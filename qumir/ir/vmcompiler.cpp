#include "vmcompiler.h"
#include "qumir/ir/vminstr.h"

#include <cassert>
#include <iostream>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

TExecFunc& TVMCompiler::Compile(const TFunction& function) {
    auto it = CodeCache.find(function.SymId);
    if (it != CodeCache.end() && it->second.UniqueId == function.UniqueId) {
        return it->second;
    }

    CodeCache[function.SymId] = TExecFunc {
        .UniqueId = function.UniqueId
    };

    auto& execFunc = CodeCache[function.SymId];
    CompileUltraLow(function, execFunc);
    return execFunc;
}

void TVMCompiler::CompileUltraLow(const TFunction& function, TExecFunc& funcOut)
{
    std::unordered_map<int64_t, int64_t> labelToPC;
    std::unordered_map<int64_t, int64_t> labelToLastPC;

    auto& code = funcOut.VMCode;
    for (const auto& block : function.Blocks) {
        labelToPC[block.Label.Idx] = code.size();
        for (const auto& instr : block.Instrs) {
            funcOut.MaxTmpIdx = std::max(funcOut.MaxTmpIdx, instr.Dest.Idx);
            code.emplace_back(); // placeholder
        }
        labelToLastPC[block.Label.Idx] = code.size() - 1;
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
                return s.Imm.IsFloat ? Module.Types.I(EKind::F64) : Module.Types.I(EKind::I64);
            case TOperand::EType::Slot:
                return -1;
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
        } else {
            return -1; // we don't have unsigned types yet
        }
    };

    auto fconv = [&](const TInstr& ins, TVMInstr& out) {
        for (int i = 0; i < ins.OperandCount; i++) {
            if (ins.Operands[i].Type == TOperand::EType::Imm && !ins.Operands[i].Imm.IsFloat) {
                double tmp = static_cast<double>(ins.Operands[i].Imm.Value);
                out.Operands[i+1] = TUntypedImm{.Value = std::bit_cast<int64_t>(tmp)};
            }
        }
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
            case '+'_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    fconv(ins, out);
                    out.Op = EVMOp::FAdd;
                } else {
                    out.Op = EVMOp::IAdd;
                }
                break;
            }
            case '-'_op: {
                require(ins, 1, 2);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    fconv(ins, out);
                    out.Op = EVMOp::FSub;
                } else {
                    out.Op = EVMOp::ISub;
                }
                break;
            }
            case '*'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    fconv(ins, out);
                    out.Op = EVMOp::FMul;
                } else {
                    out.Op = EVMOp::IMulS;
                }
                break;
            }
            case '/'_op: {
                require(ins, 1, 2);
                auto t = typeId(out.Operands[0].Tmp);
                if (Module.Types.IsFloat(t)) {
                    fconv(ins, out);
                    out.Op = EVMOp::FDiv;
                } else {
                    out.Op = EVMOp::IDivS;
                }
                break;
            }
            case '<'_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpLT;
                } else if (cType == 1) {
                    out.Op = EVMOp::ICmpLTU;
                } else {
                    out.Op = EVMOp::ICmpLTS;
                }
                break;
            }
            case '>'_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpGT;
                } else if (cType == 1) {
                    out.Op = EVMOp::ICmpGTU;
                } else {
                    out.Op = EVMOp::ICmpGTS;
                }
                break;
            }
            case "<="_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpLE;
                } else if (cType == 1) {
                    out.Op = EVMOp::ICmpLEU;
                } else {
                    out.Op = EVMOp::ICmpLES;
                }
                break;
            }
            case ">="_op: {
                require(ins, 1, 2);
                auto cType = cmpType(ins);
                if (cType == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpGE;
                } else if (cType == 1) {
                    out.Op = EVMOp::ICmpGEU;
                } else {
                    out.Op = EVMOp::ICmpGES;
                }
                break;
            }
            case "=="_op: {
                require(ins, 1, 2);
                if (cmpType(ins) == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpEQ;
                } else {
                    out.Op = EVMOp::ICmpEQ;
                }
                break;
            }
            case "!="_op: {
                require(ins, 1, 2);
                if (cmpType(ins) == 0) {
                    fconv(ins, out);
                    out.Op = EVMOp::FCmpNE;
                } else {
                    out.Op = EVMOp::ICmpNE;
                }
                break;
            }
            case "neg"_op: {
                require(ins, 1, 1);
                if (Module.Types.IsFloat(typeId(out.Operands[0].Tmp))) {
                    fconv(ins, out);
                    out.Op = EVMOp::FNeg;
                } else {
                    out.Op = EVMOp::INeg;
                }
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
                out.Op = EVMOp::Mov;
                break;
            }
            case "cmov"_op: {
                require(ins, 1, 1);
                out.Op = EVMOp::Cmov;
                break;
            }
            case "i2f"_op: {
                require(ins, 1, 1);
                out.Op = EVMOp::I2F;
                break;
            }
            case "phi2"_op: {
                require(ins, 1, 4);
                TVMInstr* left = &code[labelToLastPC.at(ins.Operands[1].Label.Idx)];
                TVMInstr* right = &code[labelToLastPC.at(ins.Operands[3].Label.Idx)];
                left--; right--;
                // write to the same register in both branches
                right->Operands[0] = left->Operands[0];
                out.Operands[1] = left->Operands[0];
                right++;
                if (right->Op == EVMOp::Cmp) {
                    right->Operands[0] = left->Operands[0];
                }
                out.Op = EVMOp::Mov; // replace phi with copy
                break;
            }
            case "arg"_op: {
                require(ins, -1, 1);
                if (ins.Operands[0].Type == TOperand::EType::Tmp) {
                    out.Op = EVMOp::ArgTmp;
                } else if (ins.Operands[0].Type == TOperand::EType::Imm) {
                    out.Op = EVMOp::ArgConst;
                } else {
                    throw std::runtime_error("arg operand must be Imm or Tmp");
                }
                break;
            }
            case "call"_op: {
                require(ins, 0, 1);

                const int64_t calleeId = ins.Operands[0].Imm.Value;
                assert(Module.SymIdToFuncIdx.contains(calleeId) && "Invalid callee id");
                const int64_t calleeIdx = Module.SymIdToFuncIdx.at(calleeId);
                assert(calleeIdx >=0 && calleeIdx < Module.Functions.size() && "Invalid callee idx");

                if (ins.Dest.Idx < 0) {
                    out.Operands[0] = TTmp{-1}; // no dest
                }

                out.Operands[1] = TImm{calleeIdx};

                out.Op = EVMOp::Call;
                break;
            }
            case "ret"_op: {
                if (ins.OperandCount == 0) {
                    out.Op = EVMOp::RetVoid;
                } else {
                    out.Op = EVMOp::Ret;
                }
                break;
            }
            case "load"_op: {
                require(ins, 1, 1);
                out.Op = EVMOp::Load64;
                break;
            }
            case "stre"_op: {
                require(ins, 0, 2);
                out.Op = EVMOp::Store64;
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