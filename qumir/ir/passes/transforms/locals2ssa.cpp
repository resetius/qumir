#include "locals2ssa.h"

#include <qumir/ir/passes/analysis/cfg.h>
#include <unordered_set>

#include <iostream>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace NPasses {

namespace {

struct PhiInfo {
    int Local;
    TTmp DstTmp;
    std::vector<TOperand> ArgTmp; // size = number of predecessors
};

struct TSSABuilder {
    TSSABuilder(TModule& module, TFunction& function)
        : Module(module)
        , Function(function)
    {}

    void ReplaceTmpEverywhere(TOperand fromTmp, TOperand toTmp) {
        for (int i = 0; i < (int)Function.Blocks.size(); ++i) {
            ReplaceTmpInBlock(i, fromTmp, toTmp);
        }
    }

    void ReplaceTmpInBlock(int blockIdx, TOperand fromTmp, TOperand toTmp) {
        if (toTmp.Type != TOperand::EType::Tmp) {
            throw std::runtime_error("Cannot replace phi destination with non-tmp operand");
        }

        auto& block = Function.Blocks[blockIdx];
        for (auto& phi : block.Phis) {
            if (phi.Dest == fromTmp) {
                phi.Dest = toTmp.Tmp;
            }
            for (auto& a : phi.Operands) {
                if (a == fromTmp) {
                    a = toTmp;
                }
            }
        }
        for (auto& inst : block.Instrs) {
            if (inst.Dest == fromTmp) {
                inst.Dest = toTmp.Tmp;
            }
            for (auto& op : inst.Operands) {
                if (op == fromTmp) {
                    op = toTmp;
                }
            }
        }
    }

    TPhi* MaterializePhiInstr(int blockIdx, const PhiInfo& ph) {
        auto& block = Function.Blocks[blockIdx];
        TPhi instr = {"phi"_op};
        instr.Dest = ph.DstTmp;
        // Args: [ (arg0, pred0), (arg1, pred1), ... ]
        instr.Operands.resize(ph.ArgTmp.size() * 2);
        auto pred = block.Pred.begin();
        for (size_t i = 0; i < ph.ArgTmp.size(); ++i) {
            instr.Operands[2*i] = ph.ArgTmp[i];
            instr.Operands[2*i+1] = TLabel{pred->Idx};
            ++pred;
        }
        return &block.Phis.emplace_back(instr);
    }

    std::optional<TOperand> MaybeCurrent(int localIdx, int blockIdx) {
        auto it = CurrentDef.find(localIdx);
        if (it != CurrentDef.end()) {
            auto it2 = it->second.find(blockIdx);
            if (it2 != it->second.end()) {
                return it2->second;
            }
        }
        return {};
    }

    void WriteVariable(int localIdx, int blockIdx, TOperand value) {
        CurrentDef[localIdx][blockIdx] = value;
    }

    TOperand ReadVariable(int localIdx, int blockIdx) {
        if (auto maybeCurrent = MaybeCurrent(localIdx, blockIdx)) {
            return *maybeCurrent;
        }

        return ReadVariableRecursive(localIdx, blockIdx);
    }

    TOperand ReadVariableRecursive(int localIdx, int blockIdx) {
        TOperand result;
        auto& block = Function.Blocks[blockIdx];
        if (!SealedBlocks.contains(blockIdx)) {
            auto newTmp = TTmp{Function.NextTmpIdx++};
            IncompletePhis[blockIdx].push_back(PhiInfo {
                .Local = localIdx,
                .DstTmp = newTmp,
            });
            Function.SetType(newTmp, Function.LocalTypes[localIdx]);
            result = newTmp;
        } else if (block.Pred.size() == 1) {
            result = ReadVariable(localIdx, block.Pred.front().Idx);
        } else {
            auto dst = TTmp{Function.NextTmpIdx++};
            PhiInfo phi {
                .Local = localIdx,
                .DstTmp = dst,
            };
            Function.SetType(dst, Function.LocalTypes[localIdx]);
            WriteVariable(localIdx, blockIdx, dst);
            result = AddPhiOperands(localIdx, blockIdx, phi);
        }
        WriteVariable(localIdx, blockIdx, result);
        return result;
    }

    TOperand AddPhiOperands(int localIdx, int blockIdx, PhiInfo& phi) {
        for (auto pred : Function.Blocks[blockIdx].Pred) {
            phi.ArgTmp.push_back(ReadVariable(localIdx, pred.Idx));
        }

        auto* instr = MaterializePhiInstr(blockIdx, phi);
        return tryRemoveTrivialPhi(*instr);
    }

    TOperand tryRemoveTrivialPhi(TPhi& phi) {
        std::optional<TOperand> same;
        for (int i = 0; i < (int)phi.Operands.size(); ++i) {
            auto& op = phi.Operands[i];
            if (op.Type == TOperand::EType::Label) {
                continue;
            }
            if (same.has_value()) {
                if (op == *same || op == TOperand{phi.Dest}) {
                    // ok
                    continue;
                } else {
                    // not trivial
                    return TOperand{phi.Dest};
                }
            } else {
                same = op;
            }
        }
        if (!same) {
            // undef: TODO
            same = TOperand{TImm{0, EKind::I64}};
        }
        //
        auto [users, phiUsers] = GetUsers(phi.Dest);
        // Reroute all uses of phi to same and remove phi
        for (auto* use : users) {
            for (int i = 0; i < use->OperandCount; ++i) {
                if (use->Operands[i] == TOperand{phi.Dest}) {
                    use->Operands[i] = *same;
                }
            }
        }
        for (auto* use : phiUsers) {
            if (use == &phi) {
                continue;
            }
            for (int i = 0; i < (int)use->Operands.size(); ++i) {
                if (use->Operands[i] == TOperand{phi.Dest}) {
                    use->Operands[i] = *same;
                }
            }
        }
        for (auto& [loc, byBlock] : CurrentDef) {
            for (auto& [b, val] : byBlock) {
                if (val == TOperand{phi.Dest}) {
                    val = *same;
                }
            }
        }

        // Try to recursively remove all phi users, which might have become trivial
        for (auto* use : phiUsers) {
            if (use == &phi) {
                continue;
            }
            if (use->Op == "phi"_op) {
                tryRemoveTrivialPhi(*use);
            }
        }
        phi.Op = "nop"_op; // mark as removed
        phi.Operands.clear();
        return *same;
    }

    // TODO: optimize by caching users per tmp
    std::pair<std::vector<TInstr*>,std::vector<TPhi*>> GetUsers(TTmp tmp) {
        std::vector<TInstr*> users;
        std::vector<TPhi*> phiUsers;
        for (int i = 0; i < (int)Function.Blocks.size(); ++i) {
            auto& block = Function.Blocks[i];
            for (auto& phi : block.Phis) {
                for (int j = 0; j < (int)phi.Operands.size(); ++j) {
                    if (phi.Operands[j].Type == TOperand::EType::Tmp &&
                        phi.Operands[j].Tmp == tmp) {
                        phiUsers.push_back(&phi);
                    }
                }
            }
            for (auto& instr : block.Instrs) {
                for (int j = 0; j < instr.OperandCount; ++j) {
                    if (instr.Operands[j].Type == TOperand::EType::Tmp &&
                        instr.Operands[j].Tmp == tmp) {
                        users.push_back(&instr);
                    }
                }
            }
        }
        return {users, phiUsers};
    }

    void SealBlock(int blockIdx) {
        if (SealedBlocks.contains(blockIdx)) {
            return;
        }
        SealedBlocks.insert(blockIdx);

        auto maybeIncompletePhis = IncompletePhis.find(blockIdx);
        if (maybeIncompletePhis == IncompletePhis.end()) {
            return;
        }
        auto& incompletePhis = maybeIncompletePhis->second;
        for (auto& phi : incompletePhis) {
            AddPhiOperands(phi.Local, blockIdx, phi);
        }
    }

    void Run() {
        BuildCfg(Function);
        auto rpo = ComputeRPO(Function.Blocks);
        std::vector<int> openPredCount(Function.Blocks.size());
        for (int i = 0; i < (int)Function.Blocks.size(); ++i) {
            openPredCount[i] = Function.Blocks[i].Pred.size();
        }

        auto remove = [&](TInstr& i) {
            i.Op = "nop"_op; // mark as removed
            i.Dest = TTmp{-1};
            i.OperandCount = 0;
        };

        // SSA conversion
        for (auto blockIdx : rpo) {
            auto& block = Function.Blocks[blockIdx];
            for (auto& instr : block.Instrs) {
                switch (instr.Op) {
                    case "stre"_op: {
                        if (instr.OperandCount != 2) {
                            throw std::runtime_error("Store instruction must have exactly two operands");
                        }
                        int localIdx = instr.Operands[0].Local.Idx;
                        if (localIdx >= Function.ArgLocals.size()) {
                            auto valueTmp = instr.Operands[1];
                            if (valueTmp.Type == TOperand::EType::Imm) {
                                auto imm = valueTmp.Imm;
                                valueTmp = TOperand{TTmp{Function.NextTmpIdx++}};
                                instr = TInstr{
                                    .Op = "cmov"_op,
                                    .Dest = valueTmp.Tmp,
                                    .Operands = { imm },
                                    .OperandCount = 1
                                };
                                Function.SetType(valueTmp.Tmp, Module.Types.I(imm.Kind));
                            } else {
                                remove(instr);
                            }
                            WriteVariable(localIdx, blockIdx, valueTmp);
                        }
                        break;
                    }
                    case "load"_op: {
                        if (instr.OperandCount != 1) {
                            throw std::runtime_error("Load instruction must have exactly one operand");
                        }
                        if (instr.Operands[0].Type != TOperand::EType::Local) {
                            throw std::runtime_error("Load instruction operand must be a local");
                        }
                        int localIdx = instr.Operands[0].Local.Idx;
                        if (localIdx >= Function.ArgLocals.size()) {
                            auto valueTmp = ReadVariable(localIdx, blockIdx);
                            auto oldDest = instr.Dest;
                            remove(instr);
                            ReplaceTmpEverywhere(TOperand{oldDest}, valueTmp);
                            CurrentDef[localIdx][blockIdx] = valueTmp;
                        }
                        break;
                    }
                    default: {
                        break;
                    }
                }
            }
            for (const auto& succ : block.Succ) {
                int s = succ.Idx;
                if (openPredCount[s] > 0) {
                    --openPredCount[s];
                    if (openPredCount[s] == 0) {
                        SealBlock(s);
                    }
                }
            }
        }
    }

    TModule& Module;
    TFunction& Function;

    // local -> (block -> tmp)
    std::unordered_map<int, std::unordered_map<int, TOperand>> CurrentDef;
    // block -> incomplete phis
    std::unordered_map<int, std::vector<PhiInfo>> IncompletePhis;
    // block indices
    std::unordered_set<int> SealedBlocks;
    // Deferred tmp replacements collected while processing blocks (from tmp -> to operand)
    std::vector<std::pair<TOperand, TOperand>> GlobalPendingReplacements;
};

} // anonymous namespace

// Simple and Efficient Construction of Static Single Assignment Form
// Matthias Braun1, Sebastian Buchwald1, Sebastian Hack2, Roland Leißa2, Christoph Mallon2, and Andreas Zwinkau
void PromoteLocalsToSSA(TFunction& function, TModule& module)
{
    TSSABuilder(module, function).Run();
}

void PromoteLocalsToSSA(TModule& module) {
    for (auto& function : module.Functions) {
        PromoteLocalsToSSA(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir