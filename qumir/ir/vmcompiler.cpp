#include "vmcompiler.h"
#include <qumir/align.h>
#include <qumir/ir/type.h>
#include <qumir/ir/vminstr.h>
#include <qumir/ir/passes/transforms/pipeline.h>

#include <cassert>
#include <iostream>
#include <iomanip>
#include <dlfcn.h>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace {

struct TPackedFunction : public NFFI::IFunction {
    using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);

    explicit TPackedFunction(TPacked packed)
        : Packed(packed)
    { }

    uint64_t operator() (const uint64_t* args, size_t argCount) override {
        return Packed(args, argCount);
    }

    TPacked Packed;
};

// SysV register class of a struct (interpreted per target ABI inside the FFI):
// >16 bytes is Memory; otherwise each eightbyte is SSE only when every field
// overlapping it is floating point.
NFFI::EStructKind ClassifyStruct(int typeId, const TTypeTable& tt, size_t size) {
    if (size == 0 || size > 16) {
        return NFFI::EStructKind::Memory;
    }
    bool sse[2] = {true, true};
    size_t offset = 0;
    for (int fieldType : tt.GetStructFields(typeId)) {
        size_t fieldSize = static_cast<size_t>(tt.SizeInBytes(fieldType));
        if (fieldSize == 0) {
            continue;
        }
        offset = (offset + fieldSize - 1) / fieldSize * fieldSize; // natural alignment
        bool isFloat = tt.IsFloat(fieldType);
        for (size_t eb = offset / 8; eb <= (offset + fieldSize - 1) / 8 && eb < 2; ++eb) {
            if (!isFloat) {
                sse[eb] = false;
            }
        }
        offset += fieldSize;
    }
    if (size <= 8) {
        return sse[0] ? NFFI::EStructKind::Sse : NFFI::EStructKind::Int;
    }
    if (sse[0] && sse[1]) {
        return NFFI::EStructKind::SseSse;
    }
    if (sse[0]) {
        return NFFI::EStructKind::SseInt;
    }
    if (sse[1]) {
        return NFFI::EStructKind::IntSse;
    }
    return NFFI::EStructKind::IntInt;
}

bool Is128BitInteger(const TTypeTable& tt, int typeId) {
    if (typeId < 0) {
        return false;
    }
    auto kind = tt.GetKind(typeId);
    return kind == EKind::I128 || kind == EKind::U128;
}

EKind ClassifyKind(int typeId, const TTypeTable& tt, NFFI::EStructKind& structKind) {
    EKind kind = tt.GetKind(typeId);
    structKind = (kind == EKind::Struct)
        ? ClassifyStruct(typeId, tt, static_cast<size_t>(tt.SizeInBytes(typeId)))
        : NFFI::EStructKind::None;
    return kind;
}

} // namespace

NFFI::IFunction* TVMCompiler::GetOrCreateExternalThunk(int externIdx) {
    if (auto it = ExternalThunkCache.find(externIdx); it != ExternalThunkCache.end()) {
        return it->second;
    }

    const TExternalFunction& ext = Module.ExternalFunctions[externIdx];
    std::unique_ptr<NFFI::IFunction> thunk;
    if (ext.Packed) {
        thunk = std::make_unique<TPackedFunction>(ext.Packed);
    } else {
        void* symbol = dlsym(RTLD_DEFAULT, ext.MangledName.c_str());
        if (!symbol) {
            return nullptr;
        }
        NFFI::EStructKind retStruct = NFFI::EStructKind::None;
        EKind retKind = ClassifyKind(ext.ReturnTypeId, Module.Types, retStruct);
        size_t retSize = static_cast<size_t>(Module.Types.SizeInBytes(ext.ReturnTypeId));
        std::vector<EKind> argKinds;
        std::vector<NFFI::EStructKind> argStructs;
        std::vector<size_t> argSizes;
        argKinds.reserve(ext.ArgTypes.size());
        argStructs.reserve(ext.ArgTypes.size());
        argSizes.reserve(ext.ArgTypes.size());
        for (int argType : ext.ArgTypes) {
            NFFI::EStructKind argStruct = NFFI::EStructKind::None;
            argKinds.push_back(ClassifyKind(argType, Module.Types, argStruct));
            argStructs.push_back(argStruct);
            argSizes.push_back(static_cast<size_t>(Module.Types.SizeInBytes(argType)));
        }
        thunk.reset(NFFI::BuildFFI(symbol, retKind, retStruct, retSize, argKinds, argStructs, argSizes));
        if (!thunk) {
            return nullptr;
        }
    }

    NFFI::IFunction* raw = thunk.get();
    ExternalThunks.push_back(std::move(thunk));
    ExternalThunkCache[externIdx] = raw;
    return raw;
}

TExecFunc& TVMCompiler::Compile(TFunction& function, bool printByteCode) {
    auto it = CodeCache.find(function.SymId);
    if (it != CodeCache.end() && it->second.UniqueId == function.UniqueId) {
        return it->second;
    }

    CodeCache[function.SymId] = TExecFunc {
        .UniqueId = function.UniqueId
    };

    auto& execFunc = CodeCache[function.SymId];
    NPasses::BeforeCompile(function, Module);
    CompileUltraLow(function, execFunc);
    if (printByteCode) {
        std::cerr << "Compiled function " << function.Name << " (symId=" << function.SymId << ", uniqueId=" << function.UniqueId << "):\n";
        std::cerr << "Start address: " << (uint64_t)execFunc.VMCode.data() << ", insrt size: " << sizeof(TVMInstr) << " bytes\n";
        char* p = reinterpret_cast<char*>(execFunc.VMCode.data());
        for (size_t i = 0; i < execFunc.VMCode.size(); ++i) {
            std::cerr << std::setw(4) << (uint64_t)(p + i * sizeof(TVMInstr)) << ": "<< execFunc.VMCode[i] << "\n";
        }
    }
    return execFunc;
}

void TVMCompiler::CompileUltraLow(const TFunction& function, TExecFunc& funcOut)
{
    int lowStringTypeId = Module.Types.Ptr(Module.Types.I(EKind::I8));
    std::unordered_map<int64_t, int64_t> labelToPC;
    std::unordered_map<int64_t, int64_t> labelToLastPC;

    auto& code = funcOut.VMCode;
    funcOut.TmpTypeIds = function.TmpTypes;
    for (const auto& block : function.Blocks) {
        labelToPC[block.Label.Idx] = code.size();
        for (const auto& instr : block.Instrs) {
            funcOut.MaxTmpIdx = std::max(funcOut.MaxTmpIdx, instr.Dest.Idx);
            code.emplace_back(); // placeholder
        }
        labelToLastPC[block.Label.Idx] = code.size() - 1;
    }

    // Compute byte offset for each local variable and address-backed temporary.
    // VM pointers must refer to memory owned by the current call frame; allocating
    // per instruction would make struct-heavy loops grow runtime-owned buffers.
    std::vector<int> localByteOffsets;
    {
        int offset = 0;
        for (int typeId : function.LocalTypes) {
            offset = AlignUp(offset, 8);
            localByteOffsets.push_back(offset);
            offset += Module.Types.SizeInBytes(typeId);
        }

        funcOut.TmpFrameOffsets.assign(function.TmpTypes.size(), -1);
        for (int tmpIdx = 0; tmpIdx < (int)function.TmpTypes.size(); ++tmpIdx) {
            const int typeId = function.TmpTypes[tmpIdx];
            if (typeId >= 0 && Is128BitInteger(Module.Types, typeId)) {
                funcOut.MaxTmp128Idx = std::max(funcOut.MaxTmp128Idx, tmpIdx);
            }
            if (typeId >= 0 && Module.Types.GetKind(typeId) == EKind::Struct) {
                offset = AlignUp(offset, 8);
                funcOut.TmpFrameOffsets[tmpIdx] = offset;
                offset += Module.Types.SizeInBytes(typeId);
            }
        }

        for (const auto& block : function.Blocks) {
            for (const auto& instr : block.Instrs) {
                if (instr.Op != "salloc"_op || instr.Dest.Idx < 0 || instr.OperandCount != 1) {
                    continue;
                }
                offset = AlignUp(offset, 8);
                if (instr.Dest.Idx >= (int)funcOut.TmpFrameOffsets.size()) {
                    funcOut.TmpFrameOffsets.resize(instr.Dest.Idx + 1, -1);
                }
                funcOut.TmpFrameOffsets[instr.Dest.Idx] = offset;
                offset += static_cast<int>(instr.Operands[0].Imm.Value);
            }
        }

        funcOut.NumLocals = AlignUp(offset, 8); // frame size in bytes
    }

    // Populate ArgByteOffsets and ArgTypeIds for eval
    for (const auto& argLocal : function.ArgLocals) {
        if (argLocal.Idx >= 0 && argLocal.Idx < (int)localByteOffsets.size()) {
            funcOut.ArgByteOffsets.push_back(localByteOffsets[argLocal.Idx]);
            int typeId = (argLocal.Idx < (int)function.LocalTypes.size())
                ? function.LocalTypes[argLocal.Idx] : -1;
            funcOut.ArgTypeIds.push_back(typeId);
        }
    }

    auto require = [&](const TInstr& ins, int requireDest, size_t requireOperands) {
        // requireDest = -1/0/1 = no, optional, required
        if (requireDest == 1) {
            if (ins.Dest.Idx < 0) {
                throw std::runtime_error("Instruction " + ins.Op.ToString() + " needs a destination tmp");
            }
        } else if (requireDest == 0) {
            // optional
        } else {
            // no dest
            if (ins.Dest.Idx >= 0) {
                throw std::runtime_error("Instruction " + ins.Op.ToString() + " must not have a destination tmp");
            }
        }
        if (ins.OperandCount != requireOperands) {
            throw std::runtime_error("Instruction " + ins.Op.ToString() + " needs " + std::to_string(requireOperands) + " operands");
        }
    };

    auto typeId = [&](const TTmp& t) -> int {
        if (t.Idx < 0 || t.Idx >= function.TmpTypes.size()) return -1;
        return function.TmpTypes[t.Idx];
    };

    auto typeIdOp = [&](const TOperand& s) -> int {
        switch (s.Type) {
            case TOperand::EType::Tmp:
                return typeId(s.Tmp);
            case TOperand::EType::Imm:
                return s.Imm.TypeId;
            case TOperand::EType::Slot:
                return Module.GlobalTypes[static_cast<size_t>(s.Slot.Idx)];
            case TOperand::EType::Local:
                return function.LocalTypes[static_cast<size_t>(s.Local.Idx)];
            default:
                return -1;
        }
    };

    auto cmpType = [&](const TInstr& ins) -> int {
        auto leftType = typeIdOp(ins.Operands[0]);
        auto rightType = typeIdOp(ins.Operands[1]);
        // -1 - signed
        //  0 - float
        //  1 - unsigned
        if (Module.Types.IsFloat(leftType) || Module.Types.IsFloat(rightType)) {
            return 0;
        }
        return Module.Types.IsUnsigned(leftType) ? 1 : -1;
    };

    auto cmp128 = [&](const TInstr& ins) -> bool {
        return Is128BitInteger(Module.Types, typeIdOp(ins.Operands[0]))
            || Is128BitInteger(Module.Types, typeIdOp(ins.Operands[1]));
    };

    auto isSignedInteger = [&](int typeId) -> bool {
        // The IR type table has unsigned kinds reserved, but source integer
        // lowering currently produces signed integer kinds only.
        return Module.Types.GetKind(typeId) != EKind::U8
            && Module.Types.GetKind(typeId) != EKind::U16
            && Module.Types.GetKind(typeId) != EKind::U32
            && Module.Types.GetKind(typeId) != EKind::U64
            && Module.Types.GetKind(typeId) != EKind::U128;
    };

    auto is128 = [&](int typeId) -> bool {
        return Is128BitInteger(Module.Types, typeId);
    };

    auto ins2vm = [&](const TInstr& ins, TVMInstr& out) {
        int offset = 0;
        if (ins.Dest.Idx >= 0) {
            out.Operands[0] = ins.Dest;
            offset = 1;
        }
        for (size_t i = 0; i < ins.OperandCount && i < out.Operands.size(); ++i) {
            switch (ins.Operands[i].Type) {
                case TOperand::EType::Tmp:
                    out.Operands[i + offset] = ins.Operands[i].Tmp;
                    break;
                case TOperand::EType::Slot:
                    out.Operands[i + offset] = ins.Operands[i].Slot;
                    break;
                case TOperand::EType::Local: {
                    // Translate var index → byte offset in frame
                    int varIdx = ins.Operands[i].Local.Idx;
                    int byteOffset = (varIdx >= 0 && varIdx < (int)localByteOffsets.size())
                        ? localByteOffsets[varIdx] : varIdx * 8;
                    out.Operands[i + offset] = TLocal{byteOffset};
                    break;
                }
                case TOperand::EType::Imm:
                    out.Operands[i + offset] = ins.Operands[i].Imm;
                    break;
                case TOperand::EType::Label:
                    TVMInstr* pc = &code[labelToPC.at(ins.Operands[i].Label.Idx)];
                    out.Operands[i + offset] = TImm{reinterpret_cast<int64_t>(pc)};
                    break;
            };
        }

        // TODO: check operand types
        switch (ins.Op) {
            case "salloc"_op: {
                require(ins, 1, 1);
                out.Op = EVMOp::SAlloc;
                const int tmpIdx = ins.Dest.Idx;
                if (tmpIdx < 0 || tmpIdx >= (int)funcOut.TmpFrameOffsets.size()
                    || funcOut.TmpFrameOffsets[tmpIdx] < 0)
                {
                    throw std::runtime_error("salloc temporary has no frame storage");
                }
                out.Operands[1] = TUntypedImm{funcOut.TmpFrameOffsets[tmpIdx]};
                out.Operands[2] = TUntypedImm{ins.Operands[0].Imm.Value};
                break;
            }
            case "ste"_op: {
                require(ins, 0, 2);
                int storeTypeId = typeIdOp(ins.Operands[1]);
                const int ptrTypeId = typeIdOp(ins.Operands[0]);
                if (Module.Types.IsPointer(ptrTypeId)) {
                    storeTypeId = Module.Types.UnderlyingType(ptrTypeId);
                }
                out.Op = is128(storeTypeId) ? EVMOp::Ste128 : EVMOp::Ste;
                out.Operands[2] = TUntypedImm{Module.Types.SizeInBytes(storeTypeId)};
                break;
            }
            case "lde"_op: {
                require(ins, 1, 1);
                out.Op = is128(typeId(ins.Dest)) ? EVMOp::Lde128 : EVMOp::Lde;
                out.Operands[2] = TUntypedImm{Module.Types.SizeInBytes(typeId(ins.Dest))};
                break;
            }
            case "lea"_op: {
                require(ins, 1, 1);
                out.Op = EVMOp::Lea;
                break;
            }
            case '+'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    out.Op = EVMOp::FAdd;
                } else {
                    out.Op = is128(t) ? EVMOp::IAdd128 : EVMOp::IAdd;
                }
                break;
            }
            case '-'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    out.Op = EVMOp::FSub;
                } else {
                    out.Op = is128(t) ? EVMOp::ISub128 : EVMOp::ISub;
                }
                break;
            }
            case '*'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    out.Op = EVMOp::FMul;
                } else if (is128(t)) {
                    out.Op = EVMOp::IMul128;
                } else {
                    out.Op = EVMOp::IMulS;
                }
                break;
            }
            case '/'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    out.Op = EVMOp::FDiv;
                } else if (is128(t)) {
                    out.Op = isSignedInteger(t) ? EVMOp::IDivS128 : EVMOp::IDivU128;
                } else {
                    out.Op = isSignedInteger(t) ? EVMOp::IDivS : EVMOp::IDivU;
                }
                break;
            }
            case '&'_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Bitwise '&' is not defined for float types");
                }
                out.Op = is128(typeId(out.Operands[0].Tmp)) ? EVMOp::IAnd128 : EVMOp::IAnd;
                break;
            }
            case '|'_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Bitwise '|' is not defined for float types");
                }
                out.Op = is128(typeId(out.Operands[0].Tmp)) ? EVMOp::IOr128 : EVMOp::IOr;
                break;
            }
            case '^'_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Bitwise '^' is not defined for float types");
                }
                out.Op = is128(typeId(out.Operands[0].Tmp)) ? EVMOp::IXor128 : EVMOp::IXor;
                break;
            }
            case "<<"_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Bitwise '<<' is not defined for float types");
                }
                out.Op = is128(typeId(out.Operands[0].Tmp)) ? EVMOp::IShl128 : EVMOp::IShl;
                break;
            }
            case ">>"_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    throw std::runtime_error("Bitwise '>>' is not defined for float types");
                }
                if (is128(t)) {
                    out.Op = isSignedInteger(t) ? EVMOp::IShrS128 : EVMOp::IShrU128;
                } else {
                    out.Op = isSignedInteger(t) ? EVMOp::IShrS : EVMOp::IShrU;
                }
                break;
            }
            case '<'_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    out.Op = EVMOp::FCmpLT;
                } else if (cType == 1) {
                    out.Op = cmp128(ins) ? EVMOp::ICmpLTU128 : EVMOp::ICmpLTU;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpLTS128 : EVMOp::ICmpLTS;
                }
                break;
            }
            case '>'_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    out.Op = EVMOp::FCmpGT;
                } else if (cType == 1) {
                    out.Op = cmp128(ins) ? EVMOp::ICmpGTU128 : EVMOp::ICmpGTU;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpGTS128 : EVMOp::ICmpGTS;
                }
                break;
            }
            case "<="_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    out.Op = EVMOp::FCmpLE;
                } else if (cType == 1) {
                    out.Op = cmp128(ins) ? EVMOp::ICmpLEU128 : EVMOp::ICmpLEU;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpLES128 : EVMOp::ICmpLES;
                }
                break;
            }
            case ">="_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    out.Op = EVMOp::FCmpGE;
                } else if (cType == 1) {
                    out.Op = cmp128(ins) ? EVMOp::ICmpGEU128 : EVMOp::ICmpGEU;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpGES128 : EVMOp::ICmpGES;
                }
                break;
            }
            case "=="_op: {
                require(ins, 1, 2);
                if (cmpType(ins) == 0) {
                    out.Op = EVMOp::FCmpEQ;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpEQ128 : EVMOp::ICmpEQ;
                }
                break;
            }
            case "!="_op: {
                require(ins, 1, 2);
                if (cmpType(ins) == 0) {
                    out.Op = EVMOp::FCmpNE;
                } else {
                    out.Op = cmp128(ins) ? EVMOp::ICmpNE128 : EVMOp::ICmpNE;
                }
                break;
            }
            case "neg"_op: {
                require(ins, 1, 1);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    out.Op = EVMOp::FNeg;
                } else {
                    out.Op = is128(t) ? EVMOp::INeg128 : EVMOp::INeg;
                }
                break;
            }
            case "!"_op: {
                require(ins, 1, 1);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Logical not '!' is not defined for float types");
                } else {
                    out.Op = is128(typeIdOp(ins.Operands[0])) ? EVMOp::INot128 : EVMOp::INot;
                }
                break;
            }
            case '~'_op: {
                require(ins, 1, 1);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    throw std::runtime_error("Bitwise '~' is not defined for float types");
                }
                out.Op = is128(typeId(out.Operands[0].Tmp)) ? EVMOp::IBitNot128 : EVMOp::IBitNot;
                break;
            }
            case "jmp"_op: {
                require(ins, -1, 1);
                out.Op = EVMOp::Jmp;
                break;
            }
            case "cmp"_op: {
                require(ins, -1, 3);
                out.Op = EVMOp::Cmp;
                break;
            }
            case "mov"_op: {
                require(ins, 1, 1);
                const bool isImm = ins.Operands[0].Type == TOperand::EType::Imm;
                const int dstType = typeId(ins.Dest);
                const int srcType = typeIdOp(ins.Operands[0]);
                if (is128(dstType)) {
                    // A 128-bit literal carries its low half only, so an immediate
                    // widens exactly like a 64-bit register does.
                    const bool signExtend = isImm ? isSignedInteger(dstType) : isSignedInteger(srcType);
                    if (is128(srcType) && !isImm) {
                        out.Op = EVMOp::Mov128;
                    } else if (isImm) {
                        out.Op = signExtend ? EVMOp::CmovS128 : EVMOp::CmovU128;
                    } else {
                        out.Op = signExtend ? EVMOp::SExt128 : EVMOp::ZExt128;
                    }
                } else if (is128(srcType) && !isImm) {
                    out.Op = EVMOp::Trunc128;
                } else {
                    out.Op = isImm ? EVMOp::Cmov : EVMOp::Mov;
                }
                break;
            }
            case "i2b"_op: {
                require(ins, 1, 1);
                out.Op = is128(typeIdOp(ins.Operands[0])) ? EVMOp::I2B128 : EVMOp::Mov;
                break;
            }
            case "i2f"_op: {
                require(ins, 1, 1);
                const int srcType = typeIdOp(ins.Operands[0]);
                if (is128(srcType)) {
                    out.Op = isSignedInteger(srcType) ? EVMOp::I2F128S : EVMOp::I2F128U;
                } else {
                    out.Op = EVMOp::I2F;
                }
                break;
            }
            case "f2b"_op:
            case "f2i"_op: {
                require(ins, 1, 1);
                out.Op = is128(typeId(ins.Dest)) ? EVMOp::F2I128 : EVMOp::F2I;
                break;
            }
            case "bitcast"_op: {
                require(ins, 1, 1);
                if (is128(typeId(ins.Dest)) || is128(typeIdOp(ins.Operands[0]))) {
                    out.Op = EVMOp::Mov128;
                } else {
                    out.Op = EVMOp::Bitcast;
                }
                break;
            }
            case "arg"_op: {
                require(ins, -1, 1);
                if (ins.Operands[0].Type == TOperand::EType::Tmp) {
                    out.Op = is128(typeIdOp(ins.Operands[0])) ? EVMOp::ArgTmp128 : EVMOp::ArgTmp;
                } else if (ins.Operands[0].Type == TOperand::EType::Imm) {
                    out.Op = EVMOp::ArgConst;
                } else {
                    throw std::runtime_error("arg operand must be Imm or Tmp");
                }
                // convert id to pointer
                if (ins.Operands[0].Type == TOperand::EType::Imm) {
                    auto imm = ins.Operands[0].Imm;
                    if (imm.TypeId == lowStringTypeId) { // string literal
                        int id = (int)imm.Value;
                        if (id < 0 || id >= Module.StringLiterals.size()) {
                            throw std::runtime_error("Invalid string literal id in outs");
                        }
                        auto& str = Module.StringLiterals[id];
                        out.Operands[0] = TImm{(int64_t)str.c_str(), lowStringTypeId};
                    }
                }
                break;
            }
            case "call"_op: {
                require(ins, 0, 1);

                const int64_t calleeId = ins.Operands[0].Imm.Value;

                if (ins.Dest.Idx < 0) {
                    out.Operands[0] = TTmp{-1}; // no dest
                }

                if (Module.SymIdToFuncIdx.contains(calleeId)) {
                    const int64_t calleeIdx = Module.SymIdToFuncIdx.at(calleeId);
                    assert(calleeIdx >=0 && calleeIdx < Module.Functions.size() && "Invalid callee idx");
                    out.Operands[1] = TImm{calleeIdx};
                    out.Op = EVMOp::Call;
                } else if (Module.SymIdToExtFuncIdx.contains(calleeId)) {
                    const int64_t calleeIdx = Module.SymIdToExtFuncIdx.at(calleeId);
                    assert(calleeIdx >=0 && calleeIdx < Module.ExternalFunctions.size() && "Invalid callee idx");
                    NFFI::IFunction* thunk = GetOrCreateExternalThunk(static_cast<int>(calleeIdx));
                    if (!thunk) {
                        throw std::runtime_error(
                            "cannot resolve external function `"
                            + Module.ExternalFunctions[calleeIdx].MangledName + "'");
                    }
                    out.Operands[1] = TImm{reinterpret_cast<int64_t>(thunk)};
                    out.Op = EVMOp::ECall;
                } else {
                    throw std::runtime_error("Unknown callee id in call: " + std::to_string(calleeId));
                }

                break;
            }
            case "await"_op: {
                if (ins.Dest.Idx < 0) {
                    out.Op = EVMOp::AwaitVoid;
                } else {
                    out.Op = EVMOp::Await;
                }
                break;
            }
            case "ret"_op: {
                if (ins.OperandCount == 0) {
                    out.Op = EVMOp::RetVoid;
                } else if (is128(typeIdOp(ins.Operands[0]))) {
                    out.Op = EVMOp::Ret128;
                } else {
                    out.Op = EVMOp::Ret;
                }
                break;
            }
            case "load"_op: {
                require(ins, 1, 1);
                int destType = typeId(ins.Dest);
                if (destType >= 0 && Module.Types.GetKind(destType) == EKind::Struct) {
                    // IR load is a struct value; VM represents struct values as 64-bit addresses.
                    out.Op = EVMOp::Lea;
                } else if (is128(destType)) {
                    out.Op = EVMOp::Load128;
                } else {
                    out.Op = EVMOp::Load64;
                }
                break;
            }
            case "stre"_op: {
                require(ins, 0, 2);
                // If destination local has struct type, emit StructStore (dst=Local, src=Tmp ptr, size)
                if (ins.Operands[0].Type == TOperand::EType::Local) {
                    int varIdx = ins.Operands[0].Local.Idx;
                    int dstTypeId = (varIdx >= 0 && varIdx < (int)function.LocalTypes.size())
                        ? function.LocalTypes[varIdx] : -1;
                    if (dstTypeId >= 0 && Module.Types.GetKind(dstTypeId) == EKind::Struct) {
                        out.Op = EVMOp::StructStore;
                        out.Operands[2] = TUntypedImm{Module.Types.SizeInBytes(dstTypeId)};
                        break;
                    }
                }
                out.Op = is128(typeIdOp(ins.Operands[1])) ? EVMOp::Store128 : EVMOp::Store64;
                break;
            }
            case "copy"_op: {
                require(ins, -1, 3); // no dest, args: dst_ptr(Tmp), src_ptr(Tmp), size_imm
                out.Op = EVMOp::Copy;
                break;
            }
            default:
                throw std::runtime_error("Unknown instruction in CompileUltraLow: " + ins.Op.ToString());
        }
    };

    auto* ptr = code.data();
    for (const auto& block : function.Blocks) {
        for (const auto& ins : block.Instrs) {
            auto& dst = *ptr++;
            ins2vm(ins, dst);
        }
    }
}

} // namespace NIR
} // namespace NQumir
