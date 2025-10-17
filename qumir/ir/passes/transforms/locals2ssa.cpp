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

    void Run() {
        BuildCfg(Function);
        auto rpo = ComputeRPO(Function.Blocks);
        std::vector<int> openPredCount(Function.Blocks.size());
        for (int i = 0; i < (int)Function.Blocks.size(); ++i) {
            openPredCount[i] = Function.Blocks[i].Pred.size();
        }

        for (auto blockIdx : rpo) {
            auto& block = Function.Blocks[blockIdx];
            std::vector<TInstr> newInstrs;
            for (const auto& instr : block.Instrs) {
                switch (instr.Op) {
                    case "stre"_op: {
                        if (instr.OperandCount != 2) {
                            throw std::runtime_error("Store instruction must have exactly two operands");
                        }
                        int localIdx = instr.Operands[0].Local.Idx;
                        if (localIdx >= Function.ArgLocals.size()) {
                            // do not optimize arguments of the function
                            auto valueTmp = instr.Operands[1];
                            if (valueTmp.Type == TOperand::EType::Imm) {
                                auto imm = valueTmp.Imm;
                                valueTmp = TOperand{TTmp{Function.NextTmpIdx++}};
                                newInstrs.push_back(TInstr{
                                    .Op = "cmov"_op,
                                    .Dest = valueTmp.Tmp,
                                    .Operands = { imm },
                                    .OperandCount = 1
                                });
                            }
                            WriteVariable(localIdx, blockIdx, valueTmp);
                        } else {
                            std::cerr << "Ignoring store to local " << localIdx << " in block " << blockIdx << "\n";
                            std::cerr << "  " << Function.ArgLocals.size() << " locals in function\n";
                            newInstrs.push_back(instr);
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
                            ReplaceTmpInBlock(blockIdx, instr.Dest, valueTmp);
                        } else {
                            std::cerr << "Ignoring load from local " << localIdx << " in block " << blockIdx << "\n";
                            std::cerr << "  " << Function.ArgLocals.size() << " locals in function\n";
                            newInstrs.push_back(instr);
                        }
                        break;
                    }
                    default: {
                        newInstrs.push_back(instr);
                        break;
                    }
                }
            }
            block.Instrs = std::move(newInstrs);
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

    void SealBlock(int blockIdx) {
        if (SealedBlocks.contains(blockIdx)) {
            return;
        }
        SealedBlocks.insert(blockIdx);

        auto it = IncompletePhis.find(blockIdx);
        if (it == IncompletePhis.end()) {
            return;
        }

        auto& block = Function.Blocks[blockIdx];
        for (auto& phiInfo : it->second) {
            for (const auto& pred : block.Pred) {
                auto tmp = ReadVariable(phiInfo.Local, pred.Idx);
                phiInfo.ArgTmp.emplace_back(tmp);
            }

            if (auto rep = TrivialPhiCollapse(phiInfo)) {
                ReplaceTmpEverywhere(phiInfo.DstTmp, *rep);
                RemovePhi(blockIdx, phiInfo.DstTmp);
                WriteVariable(phiInfo.Local, blockIdx, *rep);
            } else {
                MaterializePhiInstr(blockIdx, phiInfo);
                WriteVariable(phiInfo.Local, blockIdx, phiInfo.DstTmp);
            }
        }
        IncompletePhis.erase(it);
    }

    void RemovePhi(int blockIdx, TOperand dstTmp) {
        auto& block = Function.Blocks[blockIdx];
        auto& v = block.Phis;
        v.erase(std::remove_if(v.begin(), v.end(), [&](const TInstr& p){ return p.Dest == dstTmp; }), v.end());
    }

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

    void MaterializePhiInstr(int blockIdx, const PhiInfo& ph) {
        auto& block = Function.Blocks[blockIdx];
        TInstr instr = {"phi"_op};
        instr.Dest = ph.DstTmp;
        // Args: [ (pred0, arg0), (pred1, arg1), ... ]
        auto capacity = instr.Operands.size();
        if (ph.ArgTmp.size() * 2 > capacity) {
            std::cerr << ph.ArgTmp.size() << " " << capacity << "\n";
            throw std::runtime_error("Too many phi arguments");
        }
        auto pred = block.Pred.begin();
        for (size_t i = 0; i < ph.ArgTmp.size() && i < capacity; ++i) {
            instr.Operands[2*i] = TLabel{pred->Idx};
            instr.Operands[2*i+1] = ph.ArgTmp[i];
            ++pred;
        }
        instr.OperandCount = (int)(ph.ArgTmp.size() * 2);
        block.Phis.push_back(instr);
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

    // Collapse trivial phi by ignoring self-references (incoming equal to DstTmp)
    // example:  phi tmp(15) = label(0) tmp(2,i64) label(4) tmp(15) -> tmp(2)
    std::optional<TOperand> TrivialPhiCollapse(const PhiInfo& phiInfo) {
        std::optional<TOperand> rep;
        bool conflict = false;
        TOperand self = TOperand{phiInfo.DstTmp};
        for (const auto& a : phiInfo.ArgTmp) {
            if (a == self) {
                continue;
            }
            if (!rep) {
                rep = a;
            } else if (*rep != a) {
                conflict = true;
                break;
            }
        }

        if (!conflict && rep) {
            return rep;
        }
        return {};
    }

    void WriteVariable(int localIdx, int blockIdx, TOperand value) {
        CurrentDef[localIdx][blockIdx] = value;
    }

    TOperand ReadVariable(int localIdx, int blockIdx) {
        auto maybeCurrent = MaybeCurrent(localIdx, blockIdx);
        if (maybeCurrent) {
            return *maybeCurrent;
        }

        TOperand resultTmp;
        auto& block = Function.Blocks[blockIdx];
        int numPreds = (int)block.Pred.size();

        if (!SealedBlocks.contains(blockIdx)) {
            // incomplete phi
            if (numPreds == 0) {
                resultTmp = TTmp{-1};
            } else if (numPreds == 1) {
                resultTmp = ReadVariable(localIdx, block.Pred.front().Idx);
            } else {
                auto newTmp = TTmp{Function.NextTmpIdx++};
                IncompletePhis[blockIdx].push_back(PhiInfo {
                    .Local = localIdx,
                    .DstTmp = newTmp,
                });
                resultTmp = newTmp;
            }
            WriteVariable(localIdx, blockIdx, resultTmp);
            return resultTmp;
        }

        if (numPreds == 0) {
            resultTmp = TTmp{-1};
        } else if (numPreds == 1) {
            resultTmp = ReadVariable(localIdx, block.Pred.front().Idx);
        } else {
            auto dst = TTmp{Function.NextTmpIdx++};
            PhiInfo phi {
                .Local = localIdx,
                .DstTmp = dst,
            };
            for (auto pred : block.Pred) {
                phi.ArgTmp.push_back(ReadVariable(localIdx, pred.Idx));
            }

            MaterializePhiInstr(blockIdx, phi);
            resultTmp = dst;
        }

        WriteVariable(localIdx, blockIdx, resultTmp);
        return resultTmp;
    }

    TModule& Module;
    TFunction& Function;

    // local -> (block -> tmp)
    std::unordered_map<int, std::unordered_map<int, TOperand>> CurrentDef;
    // block -> incomplete phis
    std::unordered_map<int, std::vector<PhiInfo>> IncompletePhis;
    // block indices
    std::unordered_set<int> SealedBlocks;
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