#include "llvm_codegen.h"
#include "llvm_codegen_impl.h"

#include <qumir/ir/builder.h>

#include <memory>
#include <string>
#include <iostream>
#include <unordered_set>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Config/llvm-config.h>

// For optimization
#include <llvm/Passes/PassBuilder.h>
#include <llvm/IR/PassManager.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/Vectorize/LoopVectorize.h>

namespace NQumir::NCodeGen {

using namespace NQumir::NIR;
using namespace NQumir::NIR::NLiterals;

namespace {

llvm::Type* GetTypeById(int typeId, const TTypeTable& tt, llvm::LLVMContext& ctx) {
    if (typeId < 0) {
        // untyped, assume int64
        return llvm::Type::getInt64Ty(ctx);
    }
    switch (tt.GetKind(typeId)) {
        case EKind::I1: return llvm::Type::getInt1Ty(ctx);
        case EKind::I32: return llvm::Type::getInt32Ty(ctx);
        case EKind::I64: return llvm::Type::getInt64Ty(ctx);
        case EKind::F64: return llvm::Type::getDoubleTy(ctx);
        case EKind::Void: return llvm::Type::getVoidTy(ctx);
        case EKind::Ptr: return llvm::PointerType::get(ctx, 0); // use pointer type (i8*) across targets
        case EKind::Func: return llvm::Type::getInt64Ty(ctx); // Function Id so far, TODO
        default:
            throw std::runtime_error("unsupported primitive type");
    }
}

std::unordered_map<uint64_t, llvm::CmpInst::Predicate> icmpMap = {
    { "<"_op,  llvm::CmpInst::ICMP_SLT },
    { "<="_op, llvm::CmpInst::ICMP_SLE },
    { ">"_op,  llvm::CmpInst::ICMP_SGT },
    { ">="_op, llvm::CmpInst::ICMP_SGE },
    { "=="_op, llvm::CmpInst::ICMP_EQ },
    { "!="_op, llvm::CmpInst::ICMP_NE },
};

std::unordered_map<uint64_t, llvm::CmpInst::Predicate> fcmpMap = {
    { "<"_op,  llvm::CmpInst::FCMP_ULT },
    { "<="_op, llvm::CmpInst::FCMP_ULE },
    { ">"_op,  llvm::CmpInst::FCMP_UGT },
    { ">="_op, llvm::CmpInst::FCMP_UGE },
    { "=="_op, llvm::CmpInst::FCMP_UEQ },
    { "!="_op, llvm::CmpInst::FCMP_UNE },
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> ibinOpMap = {
    { "+"_op, llvm::Instruction::Add },
    { "-"_op, llvm::Instruction::Sub },
    { "*"_op, llvm::Instruction::Mul },
    { "/"_op, llvm::Instruction::SDiv }, // signed division
    { "%"_op, llvm::Instruction::SRem }, // signed remainder
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> ubinOpMap = {
    { "+"_op, llvm::Instruction::Add },
    { "-"_op, llvm::Instruction::Sub },
    { "*"_op, llvm::Instruction::Mul },
    { "/"_op, llvm::Instruction::UDiv }, // unsigned division
    { "%"_op, llvm::Instruction::URem }, // unsigned remainder
};

std::unordered_map<uint64_t, llvm::Instruction::BinaryOps> fbinOpMap = {
    { "+"_op, llvm::Instruction::FAdd },
    { "-"_op, llvm::Instruction::FSub },
    { "*"_op, llvm::Instruction::FMul },
    { "/"_op, llvm::Instruction::FDiv },
};

} // namespace

TLLVMCodeGen::TLLVMCodeGen(const TLLVMCodeGenOptions& opts): Opts(opts) {}
TLLVMCodeGen::~TLLVMCodeGen() = default;

std::unique_ptr<ILLVMModuleArtifacts> TLLVMCodeGen::Emit(TModule& module, int optLevel) {
    Ctx = std::make_unique<llvm::LLVMContext>();
    LModule = std::make_unique<llvm::Module>(Opts.ModuleName, *Ctx);

    auto tripleString = Opts.TargetTriple.empty()
        ? llvm::sys::getDefaultTargetTriple()
        : Opts.TargetTriple;

#if LLVM_VERSION_MAJOR <= 20
    LModule->setTargetTriple(tripleString);
#else
    LModule->setTargetTriple(llvm::Triple(tripleString));
#endif

    if (optLevel > 0) {
        CreateTargetMachine();
    }

    auto builder = std::make_unique<llvm::IRBuilder<>>(*Ctx);
    BuilderBase = std::move(builder);
    if (0) {
        // fast-math
        auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
        llvm::FastMathFlags FMF;
        FMF.setAllowContract(true);
        FMF.setAllowReassoc(true);
        FMF.setNoNaNs(true);
        FMF.setNoInfs(true);
        FMF.setAllowReciprocal(true);
        irb->setFastMathFlags(FMF);
    }
    std::unordered_set<int> newSymIds;
    // Pass 1: predeclare all functions so calls can reference them by SymId in any order
    for (const auto& f : module.Functions) {
        auto maybeUniqueIdIt = SymIdToUniqueFunId.find(f.SymId);
        if (maybeUniqueIdIt != SymIdToUniqueFunId.end()) {
            if (maybeUniqueIdIt->second == f.UniqueId) {
                continue;
            } else {
                // delete old function and re-declare
                auto* oldFun = LModule->getFunction(f.Name);
                if (oldFun) {
                    oldFun->eraseFromParent();
                }
            }
        }

        auto& ctx = *Ctx;
        std::vector<llvm::Type*> argTys(f.ArgLocals.size(), nullptr);
        for (size_t i = 0; i < f.ArgLocals.size(); ++i) {
            const auto& s = f.ArgLocals[i];
            auto typeId = f.LocalTypes[s.Idx];
            auto type = GetTypeById(typeId, module.Types, ctx);
            argTys[i] = type;
        }

        llvm::Type* retTy = GetTypeById(f.ReturnTypeId, module.Types, ctx);
        auto fty = llvm::FunctionType::get(retTy, argTys, false);
        auto lfun = LModule->getFunction(f.Name);
        if (!lfun) {
            lfun = llvm::Function::Create(fty, llvm::Function::ExternalLinkage, f.Name, LModule.get());
        } else {
            throw std::runtime_error("function already declared");
        }
        if (f.Name.find("__repl") == 0) { // repl functions should not be optimized
            lfun->addFnAttr(llvm::Attribute::NoInline);
            lfun->addFnAttr(llvm::Attribute::OptimizeNone);
            lfun->addFnAttr("disable-tail-calls", "true");
        }
        SymIdToLFun[f.SymId] = lfun;
        newSymIds.insert(f.SymId);
        SymIdToUniqueFunId[f.SymId] = f.UniqueId;
    }

    // Pass 2: lower function bodies
    int funcIdx = 0;
    std::vector<llvm::Function*> ctorFunctions;
    std::vector<llvm::Function*> dtorFunctions;
    for (const auto& f : module.Functions) {
        if (newSymIds.find(f.SymId) == newSymIds.end()) {
            funcIdx++;
            continue;
        }
        auto* function = LowerFunction(f, module);
        if (funcIdx == module.ModuleConstructorFunctionId) {
            function->setLinkage(llvm::Function::InternalLinkage);
            ctorFunctions.push_back(function);
        } else if (funcIdx == module.ModuleDestructorFunctionId) {
            function->setLinkage(llvm::Function::InternalLinkage);
            dtorFunctions.push_back(function);
        }
        funcIdx++;
    }

    auto& ctx = *Ctx;
    auto* int32Ty = llvm::Type::getInt32Ty(ctx);
    auto* voidTy = llvm::Type::getVoidTy(ctx);
    auto* voidFnTy = llvm::FunctionType::get(voidTy, false);
    // LLVM 21+: PointerType::get(ctx, AS) returns i8* for the given address space
    auto* voidFnPtrTy = llvm::PointerType::get(ctx, 0);
    auto* i8PtrTy = llvm::PointerType::get(ctx, 0);
    auto* ctorEntryTy = llvm::StructType::get(int32Ty, voidFnPtrTy, i8PtrTy);

    auto appendGlobalFuncList = [&](const char* globalName, const std::vector<llvm::Function*>& functions) {
        if (functions.empty()) {
            return;
        }

        std::vector<llvm::Constant*> newElems;
        newElems.reserve(functions.size());

        for (auto* fn : functions) {
            auto* prio = llvm::ConstantInt::get(int32Ty, 65535);
            auto* fnPtr = llvm::ConstantExpr::getBitCast(fn, voidFnPtrTy);
            auto* nullPtr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(i8PtrTy));
            newElems.push_back(llvm::ConstantStruct::get(ctorEntryTy, { prio, fnPtr, nullPtr }));
        }

        auto* existingGV = LModule->getNamedGlobal(globalName);
        if (!existingGV) {
            auto* arrTy = llvm::ArrayType::get(ctorEntryTy, newElems.size());
            auto* init = llvm::ConstantArray::get(arrTy, newElems);
            new llvm::GlobalVariable(*LModule, arrTy, /*isConstant*/false,
                                     llvm::GlobalValue::AppendingLinkage,
                                     init, globalName);
        } else {
            auto* existingArrTy = llvm::cast<llvm::ArrayType>(existingGV->getValueType());
            auto* existingInit = llvm::cast<llvm::ConstantArray>(existingGV->getInitializer());

            std::vector<llvm::Constant*> combined;
            combined.reserve(existingArrTy->getNumElements() + newElems.size());
            for (unsigned i = 0; i < existingArrTy->getNumElements(); ++i) {
                combined.push_back(existingInit->getOperand(i));
            }
            combined.insert(combined.end(), newElems.begin(), newElems.end());

            auto* newArrTy = llvm::ArrayType::get(ctorEntryTy, combined.size());
            auto* newInit = llvm::ConstantArray::get(newArrTy, combined);
            auto* newPtrTy = llvm::PointerType::get(ctx, 0);
            existingGV->mutateType(newPtrTy);
            existingGV->setInitializer(newInit);
        }
    };

    appendGlobalFuncList("llvm.global_ctors", ctorFunctions);
    appendGlobalFuncList("llvm.global_dtors", dtorFunctions);

    if (llvm::verifyModule(*LModule, &llvm::errs())) {
        llvm::errs() << "\n[LLVMCodeGen] Module verify failed. Dumping IR:\n";
        LModule->print(llvm::errs(), nullptr);
        throw std::runtime_error("LLVM verify failed");
    }

    if (optLevel > 0) {
        Optimize(optLevel);
    }

    auto out = std::make_unique<TLLVMModuleArtifacts>();
    out->Ctx = std::move(Ctx);
    out->Module = std::move(LModule);
    // Collect defined function names for tests without pulling in LLVM headers
    if (out->Module) {
        for (auto& F : *out->Module) {
            if (!F.isDeclaration()) {
                out->FunctionNames.push_back(F.getName().str());
            }
        }
    }
    return out;
}

llvm::GlobalVariable* TLLVMCodeGen::EnsureSlotGlobal(int64_t sidx, NIR::TModule& module)
{
    if (sidx < 0) throw std::runtime_error("negative slot index");
    if (sidx >= (int64_t)ModuleSlots.size()) ModuleSlots.resize(sidx + 1, nullptr);
    if (!ModuleSlots[sidx]) {
        auto *slotTy = GetTypeById(module.GlobalTypes[sidx], module.Types, *Ctx);
        llvm::Constant* init = nullptr;
        if (slotTy->isFloatingPointTy()) {
            init = llvm::ConstantFP::get(slotTy, 0.0);
        } else if (slotTy->isPointerTy()) {
            init = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(slotTy));
        } else if (slotTy->isIntegerTy()) {
            init = llvm::ConstantInt::get(slotTy, 0, /*isSigned*/true);
        } else {
            init = llvm::UndefValue::get(slotTy);
        }
        auto *g = new llvm::GlobalVariable(*LModule, slotTy, /*isConstant*/false, llvm::GlobalValue::InternalLinkage,
            init,
            "slot" + std::to_string(sidx));
        ModuleSlots[sidx] = g;
    }
    return static_cast<llvm::GlobalVariable*>(ModuleSlots[sidx]);
}

llvm::Function* TLLVMCodeGen::LowerFunction(const TFunction& fun, NIR::TModule& module) {
    auto& ctx = *Ctx;
    auto lfun = LModule->getFunction(fun.Name);
    // Function has already been registered in Emit pre-pass

    CurFun = std::make_unique<TFunState>();
    CurFun->Fun = &fun;
    CurFun->LFun = lfun;
    CurFun->TmpValues.resize(fun.NextTmpIdx, nullptr);

    std::vector<llvm::BasicBlock*> bbs; bbs.reserve(fun.Blocks.size());
    for (const auto& b : fun.Blocks) {
        auto *bb = llvm::BasicBlock::Create(ctx, "bb" + std::to_string(b.Label.Idx), lfun);
        bbs.push_back(bb);
        CurFun->LabelToBB[b.Label.Idx] = bb;
    }

    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    irb->SetInsertPoint(bbs.front());

    CurFun->Allocas.resize(fun.LocalTypes.size(), nullptr);
    for (int i = 0; i < (int)fun.LocalTypes.size(); ++i) {
        auto* localTy = GetTypeById(fun.LocalTypes[i], module.Types, ctx);
        auto* alloca = irb->CreateAlloca(localTy, nullptr, "local" + std::to_string(i));
        // TODO: remove it after removing str_release(nullptr)
        irb->CreateStore(llvm::Constant::getNullValue(localTy), alloca);
        CurFun->Allocas[i] = alloca;
    }

    for (int i = 0; i < (int)fun.ArgLocals.size(); ++i) {
        const auto& l = fun.ArgLocals[i];
        if (l.Idx < 0 || l.Idx >= (int)CurFun->Allocas.size()) {
            throw std::runtime_error("invalid argument local index");
        }
        auto* ptr = CurFun->Allocas[l.Idx];
        auto& arg = *lfun->getArg(i);
        auto* dstTy = ptr->getAllocatedType();
        irb->CreateStore(&arg, ptr);
    }

    for (size_t i = 0; i < fun.Blocks.size(); ++i) {
        irb->SetInsertPoint(bbs[i]);
        LowerBlock(fun.Blocks[i], module, lfun, bbs);
    }
    for (size_t i = 0; i < fun.Blocks.size(); ++i) {
        irb->SetInsertPoint(bbs[i]);
        for (const auto& instr : fun.Blocks[i].Phis) {
           AddIncomingPhiEdges(instr, module);
        }
    }
    return lfun;
}

void TLLVMCodeGen::LowerBlock(const TBlock& blk, NIR::TModule& module, llvm::Function*, std::vector<llvm::BasicBlock*>& orderedBBs) {
    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    for (const auto& instr : blk.Phis) {
        if (irb->GetInsertBlock()->getTerminator()) {
            throw std::runtime_error("attempt to emit instruction after terminator");
        }
        EmitPhi(instr, module);
    }

    for (const auto& instr : blk.Instrs) {
        if (irb->GetInsertBlock()->getTerminator()) {
            throw std::runtime_error("attempt to emit instruction after terminator");
        }
        LowerInstr(instr, module);
    }
}

llvm::Value* TLLVMCodeGen::GetOp(const TOperand& op, NIR::TModule& module)
{
    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    auto& ctx = irb->getContext();
    auto i64 = llvm::Type::getInt64Ty(ctx);
    auto f64 = llvm::Type::getDoubleTy(ctx);
    auto lowStringTypeId = module.Types.Ptr(module.Types.I(EKind::I8));
    auto lowFloatTypeId = module.Types.I(EKind::F64);
    auto lowIntTypeId = module.Types.I(EKind::I64);
    auto lowBoolTypeId = module.Types.I(EKind::I1);

    switch (op.Type) {
        case TOperand::EType::Imm:
            if (op.Imm.TypeId == lowFloatTypeId) {
                return llvm::ConstantFP::get(f64, std::bit_cast<double>(op.Imm.Value));
            } else if (op.Imm.TypeId == lowIntTypeId) {
                // TODO: other immediate types
                return llvm::ConstantInt::get(i64, op.Imm.Value, true);
            } else if (op.Imm.TypeId == lowStringTypeId) {
                // assume char* for now
                int id = (int)op.Imm.Value;
                if (id < 0 || id >= module.StringLiterals.size()) {
                    throw std::runtime_error("Invalid string literal id in outs");
                }
                if (id >= (int)StringLiterals.size()) {
                    StringLiterals.resize(id + 1, nullptr);
                }
                auto& str = StringLiterals[id];
                if (str) {
                    return str;
                }
                if (id == 0) {
                    // nullptr string
                    str = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(GetTypeById(lowStringTypeId, module.Types, ctx)));
                    return str;
                }

                str = irb->CreateGlobalString(module.StringLiterals[id], "strlit" + std::to_string(id));
                return StringLiterals[id] = str;
            } else if (op.Imm.TypeId == lowBoolTypeId) {
                return llvm::ConstantInt::get(llvm::Type::getInt1Ty(ctx), op.Imm.Value != 0 ? 1 : 0, false);
            } else {
                throw std::runtime_error("unsupported immediate type");
            }
        case TOperand::EType::Tmp: {
            if (op.Tmp.Idx < 0 || op.Tmp.Idx >= CurFun->TmpValues.size()) {
                throw std::runtime_error("invalid temporary index: " + std::to_string(op.Tmp.Idx));
            }
            if (!CurFun->TmpValues[op.Tmp.Idx]) {
                throw std::runtime_error("use of uninitialized temporary: " + std::to_string(op.Tmp.Idx));
            }
            return CurFun->TmpValues[op.Tmp.Idx];
        }
        default:
            throw std::runtime_error("unsupported operand type in ALU instruction");
    };
}

llvm::Value* TLLVMCodeGen::EmitPhi(const NIR::TPhi& instr, NIR::TModule& module)
{
    auto opcode = instr.Op;
    if (opcode != "phi"_op && opcode != "nop"_op) {
        throw std::runtime_error("EmitPhi called with non-phi instruction");
    }
    if (opcode == "nop"_op) {
        return nullptr;
    }

    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    auto& ctx = irb->getContext();
    llvm::Type* outputType = nullptr;
    int outputTypeId = -1;

    if (instr.Dest.Idx >= 0) {
        outputTypeId = CurFun->Fun->GetTmpType(instr.Dest.Idx);
        outputType = GetTypeById(outputTypeId, module.Types, ctx);
    }

    auto storeTmp = [&](llvm::Value* v){
        if (instr.Dest.Idx >= 0) {
            if (instr.Dest.Idx >= CurFun->TmpValues.size()) {
                CurFun->TmpValues.resize(instr.Dest.Idx + 1, nullptr);
            }
            CurFun->TmpValues[instr.Dest.Idx] = v;
        }
        return v;
    };

    auto operandCount = instr.Size();
    if (operandCount % 2 != 0 || operandCount < 2) {
        throw std::runtime_error("phi needs even number of operands >= 2");
    }

    auto phi = irb->CreatePHI(outputType, operandCount / 2, "phitmp");
    return storeTmp(phi);
}

void TLLVMCodeGen::AddIncomingPhiEdges(const NIR::TPhi& instr, NIR::TModule& module)
{
    if (instr.Op == "nop"_op) {
        return;
    }
    auto* value = CurFun->TmpValues[instr.Dest.Idx];
    auto* phi = llvm::cast<llvm::PHINode>(value);
    for (int i = 0; i < instr.Size() / 2; ++i) {
        auto value = GetOp(instr.Operands[2*i], module);
        auto label = instr.Operands[2*i+1].Label.Idx;
        auto block = CurFun->LabelToBB.find(label);
        if (block == CurFun->LabelToBB.end()) {
            throw std::runtime_error("phi incoming label not found");
        }
        phi->addIncoming(value, block->second);
    }
}

llvm::Value* TLLVMCodeGen::LowerInstr(const NIR::TInstr& instr, NIR::TModule& module) {
    auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
    auto& ctx = irb->getContext();
    auto i64 = llvm::Type::getInt64Ty(ctx);
    auto f64 = llvm::Type::getDoubleTy(ctx);
    llvm::Type* outputType = nullptr;
    int outputTypeId = -1;
    int operandCount = instr.Size();

    if (instr.Dest.Idx >= 0) {
        outputTypeId = CurFun->Fun->GetTmpType(instr.Dest.Idx);
        outputType = GetTypeById(outputTypeId, module.Types, ctx);
    }

    auto opcode = instr.Op;
    auto storeTmp = [&](llvm::Value* v){
        if (instr.Dest.Idx >= 0) {
            if (instr.Dest.Idx >= CurFun->TmpValues.size()) {
                CurFun->TmpValues.resize(instr.Dest.Idx + 1, nullptr);
            }
            CurFun->TmpValues[instr.Dest.Idx] = v;
        }
        return v;
    };

    auto cast = [&](llvm::Value* val, llvm::Type* expectedType) -> llvm::Value* {
        auto* actualTy = val->getType();

        if (actualTy == expectedType) {
            return val;
        }

        const bool actualIsPtr = actualTy->isPointerTy();
        const bool expectedIsPtr = expectedType->isPointerTy();

        // for strings
        // TODO: remove me after changing string type to proper pointer type
        if (actualIsPtr && expectedType->isIntegerTy()) {
            return irb->CreatePtrToInt(val, expectedType, "cast");
        }
        if (actualTy->isIntegerTy() && expectedIsPtr) {
            return irb->CreateIntToPtr(val, expectedType, "cast");
        }

        throw std::runtime_error("unexpected cast request; insert explicit cast earlier");
    };

    auto cmpInsr = [&](llvm::CmpInst::Predicate pred, llvm::Value* lhs, llvm::Value* rhs) -> llvm::Value* {
        auto i1v = irb->CreateCmp(pred, lhs, rhs, "cmprtmp");
        return i1v;
    };

    auto binOp = [&](llvm::Instruction::BinaryOps op) -> llvm::Value* {
        // TODO: cast after operation
        auto lhs = cast(GetOp(instr.Operands[0], module), outputType);
        auto rhs = cast(GetOp(instr.Operands[1], module), outputType);
        return irb->CreateBinOp(op, lhs, rhs, "bintmp");
    };

    switch (opcode) {
        case "+"_op:
        case "-"_op:
        case "*"_op:
        case "/"_op: {
            if (outputType == nullptr) {
                throw std::runtime_error("arithmetic op needs a typed dest");
            }
            if (outputType->isFloatingPointTy()) {
                auto it = fbinOpMap.find(opcode);
                if (it == fbinOpMap.end()) throw std::runtime_error("unsupported fbinop opcode");
                return storeTmp(binOp(it->second));
            } else if (outputType->isIntegerTy()) {
                if (true /*module.Types.IsSigned(outputTypeId) always signed*/) {
                    // signed integer ops
                    auto it = ibinOpMap.find(opcode);
                    if (it == ibinOpMap.end()) throw std::runtime_error("unsupported ibinop opcode");
                    return storeTmp(binOp(it->second));
                } else {
                    // unsigned integer ops
                    auto it = ubinOpMap.find(opcode);
                    if (it == ubinOpMap.end()) throw std::runtime_error("unsupported ubinop opcode");
                    return storeTmp(binOp(it->second));
                }
            } else if (outputType->isPointerTy()) {
                if (opcode != "+"_op && opcode != "-"_op) {
                    throw std::runtime_error("only + and - supported for pointer types");
                }
                auto lhs = GetOp(instr.Operands[0], module);
                auto rhs = GetOp(instr.Operands[1], module);
                if (opcode == "+"_op) {
                    auto gep = irb->CreateGEP(llvm::Type::getInt8Ty(ctx), lhs, rhs, "ptraddtmp");
                    return storeTmp(gep);
                } else if (opcode == "-"_op) {
                    auto negRhs = irb->CreateNeg(cast(rhs, rhs->getType()), "negtmp");
                    auto gep = irb->CreateGEP(llvm::Type::getInt8Ty(ctx), lhs, negRhs, "ptrsubtmp");
                    return storeTmp(gep);
                }
            } else {
                throw std::runtime_error("unsupported type for arithmetic op");
            }
        }
        // Relational / equality operators: produce i64 0/1
        case "<"_op:
        case "<="_op:
        case ">"_op:
        case ">="_op:
        case "=="_op:
        case "!="_op: {
            // Output type is i1 (bool)
            auto lhs = GetOp(instr.Operands[0], module);
            auto rhs = GetOp(instr.Operands[1], module);
            if (lhs->getType()->isFloatingPointTy()) {
                auto it = fcmpMap.find(opcode);
                if (it == fcmpMap.end()) throw std::runtime_error("unsupported fcmp opcode");
                return storeTmp(cmpInsr(it->second, lhs, rhs));
            } else if (lhs->getType()->isIntegerTy()) {
                // todo: unsigned?
                auto it = icmpMap.find(opcode);
                if (it == icmpMap.end()) throw std::runtime_error("unsupported icmp opcode");
                return storeTmp(cmpInsr(it->second, lhs, rhs));
            } else {
                throw std::runtime_error("unsupported type for comparison");
            }
        }
        case "!"_op: {
            auto v = GetOp(instr.Operands[0], module);
            if (!outputType || !outputType->isIntegerTy(1)) {
                throw std::runtime_error("logical not output type must be i1");
            }
            if (outputType->isIntegerTy(1)) {
                auto zero = llvm::ConstantInt::get(v->getType(), 0);
                auto cmp = irb->CreateICmpEQ(v, zero, "nottmp");
                return storeTmp(cmp);
            }
        }
        case "neg"_op: {
            auto v = GetOp(instr.Operands[0], module);
            if (outputType->isFloatingPointTy()) {
                return storeTmp(irb->CreateFNeg(cast(v, outputType), "fnegtmp"));
            } else if (outputType->isIntegerTy()) {
                return storeTmp(irb->CreateNeg(cast(v, outputType), "inegtmp"));
            }
        }
        case "lde"_op: {
            // tmp = *ptr
            auto ptr = GetOp(instr.Operands[0], module);
            auto val = irb->CreateLoad(outputType, ptr, "ldtmp");
            return storeTmp(val);
            break;
        }
        case "ste"_op: {
            // *ptr = tmp
            auto ptr = GetOp(instr.Operands[0], module);
            auto value = GetOp(instr.Operands[1], module);
            irb->CreateStore(value, ptr);
            return nullptr;
            break;
        }
        case "load"_op: {
            if (operandCount != 1 || instr.Dest.Idx < 0) throw std::runtime_error("load needs 1 operand and a dest");
            if (instr.Operands[0].Type == TOperand::EType::Slot) {
                auto idx = instr.Operands[0].Slot.Idx;
                auto g = EnsureSlotGlobal(idx, module);
                auto val = irb->CreateLoad(outputType, g, "loadtmp");
                return storeTmp(val);
            } else if (instr.Operands[0].Type == TOperand::EType::Local) {
                auto lidx = instr.Operands[0].Local.Idx;
                if (lidx < 0 || lidx >= CurFun->Allocas.size()) throw std::runtime_error("invalid local index");
                auto ptr = CurFun->Allocas[lidx];
                auto val = irb->CreateLoad(ptr->getAllocatedType(), ptr, "loadtmp");
                return storeTmp(cast(val, outputType));
            } else {
                throw std::runtime_error("load operand must be slot or local");
            }
            return nullptr;
        }
        case "lea"_op: {
            // like load byt get address of slot/local
            if (operandCount != 1 || instr.Dest.Idx < 0) throw std::runtime_error("lea needs 1 operand and a dest");
            if (instr.Operands[0].Type == TOperand::EType::Slot) {
                auto idx = instr.Operands[0].Slot.Idx;
                auto g = EnsureSlotGlobal(idx, module);
                return storeTmp(g);
            } else if (instr.Operands[0].Type == TOperand::EType::Local) {
                auto lidx = instr.Operands[0].Local.Idx;
                if (lidx < 0 || lidx >= CurFun->Allocas.size()) throw std::runtime_error("invalid local index");
                auto ptr = CurFun->Allocas[lidx];
                return storeTmp(ptr);
            } else {
                throw std::runtime_error("lea operand must be slot or local");
            }
            return nullptr;
            break;
        }
        case "stre"_op: {
            if (operandCount != 2)  throw std::runtime_error("store needs 2 operands");
            if (instr.Operands[0].Type == TOperand::EType::Slot) {
                auto idx = instr.Operands[0].Slot.Idx;
                auto g = EnsureSlotGlobal(idx, module);
                auto value = GetOp(instr.Operands[1], module);
                auto* valTy = g->getValueType();
                irb->CreateStore(cast(value, valTy), g);
            } else if (instr.Operands[0].Type == TOperand::EType::Local) {
                auto lidx = instr.Operands[0].Local.Idx;
                if (lidx < 0 || lidx >= CurFun->Allocas.size()) throw std::runtime_error("invalid local index");
                auto ptr = CurFun->Allocas[lidx];
                auto value = GetOp(instr.Operands[1], module);
                irb->CreateStore(cast(value, ptr->getAllocatedType()), ptr);
            } else {
                throw std::runtime_error("store first operand must be slot or local");
            }
            return nullptr;
        }
        case "ret"_op: {
            if (operandCount > 0) {
                auto val = GetOp(instr.Operands[0], module);
                auto* need = CurFun->LFun->getFunctionType()->getReturnType();
                if (val->getType() != need) {
                    throw std::runtime_error("return type mismatch; pre-cast required");
                }
                irb->CreateRet(val);
            } else {
                irb->CreateRetVoid();
            }
            return nullptr;
        }
        case "i2b"_op: {
            auto v = GetOp(instr.Operands[0], module);
            auto zero = llvm::ConstantInt::get(v->getType(), 0);
            auto cmp = irb->CreateICmpNE(v, zero, "i2b");
            return storeTmp(cmp);
        }
        case "f2b"_op: {
            auto v = GetOp(instr.Operands[0], module);
            auto zero = llvm::ConstantFP::get(v->getType(), 0.0);
            auto cmp = irb->CreateFCmpONE(v, zero, "f2b");
            return storeTmp(cmp);
        }
        case "i2f"_op: {
            auto v = GetOp(instr.Operands[0], module);
            if (!outputType) throw std::runtime_error("cast/mov needs typed dest");
            return storeTmp(irb->CreateSIToFP(v, outputType, "i2f"));
        }
        case "f2i"_op: {
            auto v = GetOp(instr.Operands[0], module);
            if (!outputType) throw std::runtime_error("cast/mov needs typed dest");
            return storeTmp(irb->CreateFPToSI(v, outputType, "f2i"));
        }
        case "mov"_op: {
            auto v = GetOp(instr.Operands[0], module);
            if (!outputType) throw std::runtime_error("cast/mov needs typed dest");
            if (v->getType() != outputType) {
                auto sourceType = v->getType();
                if (sourceType->isIntegerTy() && outputType->isIntegerTy()) {
                    v = irb->CreateIntCast(v, outputType, /*isSigned=*/true, "movcast");
                } else {
                    throw std::runtime_error("mov type mismatch");
                }
            }
            return storeTmp(v);
        }
        case "arg"_op: {
            if (operandCount != 1) throw std::runtime_error("arg needs 1 operand");
            auto v = GetOp(instr.Operands[0], module);
            CurFun->PendingArgs.push_back(v);
            return nullptr;
        }
        case "call"_op: {
            // IR: call has optional Dest (required only for non-void) and one operand: Imm(symId)
            if (operandCount < 1) throw std::runtime_error("call needs callee operand");
            if (instr.Operands[0].Type != TOperand::EType::Imm) throw std::runtime_error("call callee must be Imm(symId)");
            const int calleeSymId = static_cast<int>(instr.Operands[0].Imm.Value);
            llvm::FunctionCallee callee;
            auto it = SymIdToLFun.find(calleeSymId);
            if (it != SymIdToLFun.end()) {
                // internal function
                callee = it->second;
            }
            if (!callee) {
                auto jt = module.SymIdToExtFuncIdx.find(calleeSymId);
                if (jt != module.SymIdToExtFuncIdx.end()) {
                    // external function
                    auto& extFun = module.ExternalFunctions[jt->second];

                    auto* retType = GetTypeById(extFun.ReturnTypeId, module.Types, ctx);
                    std::vector<llvm::Type*> paramTys;
                    for (const auto& pid : extFun.ArgTypes) {
                        paramTys.push_back(GetTypeById(pid, module.Types, ctx));
                    }
                    auto* fty   = llvm::FunctionType::get(retType, paramTys, /*isVarArg=*/false);
                    callee = LModule->getOrInsertFunction(extFun.MangledName, fty);
                }
            }

            if (!callee) {
                throw std::runtime_error("call target function not found: " + std::to_string(calleeSymId));
            }

            auto* retTy = callee.getFunctionType()->getReturnType();
            // Marshal arguments collected by 'arg'
            auto* irb = static_cast<llvm::IRBuilder<>*>(BuilderBase.get());
            std::vector<llvm::Value*> args;
            for (int i = 0; i < CurFun->PendingArgs.size(); ++i) {
                auto* paramTy = callee.getFunctionType()->getParamType(i);
                args.push_back(cast(CurFun->PendingArgs[i], paramTy));
            }
            CurFun->PendingArgs.clear();
            if (retTy->isVoidTy()) {
                // Void call: emit and produce no tmp
                (void)irb->CreateCall(callee, args);
                return nullptr;
            } else {
                if (instr.Dest.Idx < 0) throw std::runtime_error("call needs a destination tmp");
                auto call = irb->CreateCall(callee, args, "calltmp");
                return storeTmp(call);
            }
        }
        case "jmp"_op: {
            if (operandCount != 1) throw std::runtime_error("jmp needs 1 operand");
            if (!CurFun) throw std::runtime_error("jmp not in function");
            if (instr.Operands[0].Type != TOperand::EType::Label) throw std::runtime_error("jmp operand must be label");
            int64_t lab = instr.Operands[0].Label.Idx;
            auto it = CurFun->LabelToBB.find(lab);
            if (it == CurFun->LabelToBB.end()) throw std::runtime_error("jmp target label not found");
            irb->CreateBr(it->second);
            return nullptr;
        }
        case "cmp"_op: {
            if (operandCount != 3) throw std::runtime_error("cmp needs 3 operands");
            auto condV = GetOp(instr.Operands[0], module);
            if (instr.Operands[1].Type != TOperand::EType::Label || instr.Operands[2].Type != TOperand::EType::Label) {
                throw std::runtime_error("cmp branch targets must be labels");
            }
            int64_t tLab = instr.Operands[1].Label.Idx;
            int64_t fLab = instr.Operands[2].Label.Idx;
            auto itT = CurFun->LabelToBB.find(tLab);
            auto itF = CurFun->LabelToBB.find(fLab);
            if (itT == CurFun->LabelToBB.end() || itF == CurFun->LabelToBB.end()) throw std::runtime_error("cmp branch target not found");
            llvm::Value* cmpNZ = nullptr;
            if (condV->getType()->isFloatingPointTy()) {
                auto zero = llvm::ConstantFP::get(condV->getType(), 0.0);
                cmpNZ = irb->CreateFCmpUNE(condV, zero, "cmptmp");
            } else {
                auto zero = llvm::ConstantInt::get(condV->getType(), 0, true);
                cmpNZ = irb->CreateICmpNE(condV, zero, "cmptmp");
            }
            if (itT->second == itF->second) {
                irb->CreateBr(itT->second);
            } else {
                irb->CreateCondBr(cmpNZ, itT->second, itF->second);
            }
            return nullptr;
        }
    };
    std::cerr << "LLVMCodeGen: unhandled instruction: '" << instr.Op.ToString() << "'\n";
    return nullptr;
}

void TLLVMCodeGen::CreateTargetMachine() {
    auto triple = LModule->getTargetTriple();
    std::string errStr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, errStr);
    if (!target) {
        throw std::runtime_error(std::string("lookupTarget failed: ") + errStr);
    }
    llvm::TargetOptions opt;
    auto RM = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    TM.reset(
        target->createTargetMachine(triple, "generic", "", opt, RM)
    );

    LModule->setDataLayout(TM->createDataLayout());
}

void TLLVMCodeGen::Optimize(int optLevel) {
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(TM.get());
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    auto OL = llvm::OptimizationLevel::O0;
    switch (optLevel) {
        case 0: OL = llvm::OptimizationLevel::O0; break;
        case 1: OL = llvm::OptimizationLevel::O1; break;
        case 2: OL = llvm::OptimizationLevel::O2; break;
        case 3: OL = llvm::OptimizationLevel::O3; break;
        case 4: OL = llvm::OptimizationLevel::Oz; break;
        default: OL = llvm::OptimizationLevel::O2; break;
    }
    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OL);
    if (OL == llvm::OptimizationLevel::O3) {
        MPM.addPass(llvm::createModuleToFunctionPassAdaptor(llvm::LoopVectorizePass()));
    }
    MPM.run(*LModule, MAM);
}

void TLLVMCodeGen::PrintFunction(int symId, std::ostream& os) const {
    auto it = SymIdToLFun.find(symId);
    if (it == SymIdToLFun.end()) {
        os << "[PrintFunction] function not found by SymId " << symId << "\n";
        return;
    }
    auto* f = it->second;
    if (!f) {
        os << "[PrintFunction] function pointer is null for SymId " << symId << "\n";
        return;
    }
    std::string str;
    llvm::raw_string_ostream rso(str);
    f->print(rso);
    rso.flush();
    os << str << "\n";
}

void TLLVMModuleArtifacts::PrintModule(std::ostream& os) const {
    if (!Module) {
        os << "[PrintModule] module is null\n";
        return;
    }
    std::string str;
    llvm::raw_string_ostream rso(str);
    Module->print(rso, nullptr);
    rso.flush();
    os << str << "\n";
}

} // namespace NQumir::NCodeGen
