#include "de_ssa.h"

#include <iostream>

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void DeSSA(TFunction& function, TModule& module) {
    std::vector<TBlock> newBlocks;
    std::map<std::pair<TLabel,TLabel>, TLabel> remap;
    // First, split critical edges
    for (auto& block : function.Blocks) {
        for (auto& predLabel : block.Pred) {
            auto& predBlock = function.Blocks[function.GetBlockIdx(predLabel)];
            if (predBlock.Succ.size() > 1 && block.Pred.size() > 1) {
                // critical edge, need to split
                TLabel newLabel = {function.NextLabelIdx++};
                newBlocks.push_back(TBlock {
                    .Label = newLabel,
                    .Instrs = {
                        {
                            .Op = "jmp"_op,
                            .Operands = {block.Label},
                            .OperandCount = 1,
                        }
                    },
                    .Succ = {block.Label},
                    .Pred = {predLabel},
                });
                remap[{predLabel, block.Label}] = newLabel;
                auto& termInstr = predBlock.Instrs.back();
                for (size_t i = 0; i < termInstr.OperandCount; ++i) {
                    if (termInstr.Operands[i] == block.Label) {
                        termInstr.Operands[i] = newLabel;
                    }
                }
                for (auto& succLabel : predBlock.Succ) {
                    if (succLabel == block.Label) {
                        succLabel = newLabel;
                    }
                }
                predLabel = newLabel;
            }
        }
    }
    for (const auto& newBlock : newBlocks) {
        auto idx = function.Blocks.size();
        function.Blocks.push_back(newBlock);
        if (newBlock.Label.Idx >= function.Label2Idx.size()) {
            function.Label2Idx.resize(newBlock.Label.Idx + 1, -1);
        }
        function.Label2Idx[newBlock.Label.Idx] = idx;
    }

    for (auto& block : function.Blocks) {
        // Collect all phi transfers per predecessor to preserve parallel semantics
        std::map<TLabel, std::vector<std::pair<TTmp, TOperand>>> perPred; // pred -> [(dest, src)]
        for (const auto& phi : block.Phis) {
            if (phi.Op != "phi"_op) continue; // skip nops
            for (size_t i = 0; i < phi.Operands.size(); i += 2) {
                auto src = phi.Operands[i];
                auto plabel = phi.Operands[i + 1].Label;
                auto it = remap.find({plabel, block.Label});
                if (it != remap.end()) {
                    plabel = it->second;
                }
                perPred[plabel].push_back({phi.Dest, src});
            }
        }

        // For each predecessor, insert two-phase copies: capture sources into fresh tmps, then assign to dests
        for (const auto& entry : perPred) {
            const TLabel predLabel = entry.first;
            const auto& pairs = entry.second;
            auto& predBlock = function.Blocks[function.GetBlockIdx(predLabel)];
            if (predBlock.Instrs.empty()) {
                throw std::runtime_error("DeSSA: cannot insert phi assignment into empty predecessor block: " + std::to_string(predLabel.Idx));
            }
            // Build sequences
            std::vector<TInstr> preCopies; preCopies.reserve(pairs.size());
            std::vector<TInstr> postCopies; postCopies.reserve(pairs.size());
            std::vector<TTmp> temps; temps.reserve(pairs.size());
            for (const auto& [dest, src] : pairs) {
                // fresh tmp of the same type as dest
                TTmp t{ function.NextTmpIdx++ };
                function.SetType(t, function.GetTmpType(dest.Idx));
                temps.push_back(t);
                // t = src
                preCopies.push_back(TInstr{
                    .Op = "mov"_op,
                    .Dest = t,
                    .Operands = { src },
                    .OperandCount = 1,
                });
                // dest = t
                postCopies.push_back(TInstr{
                    .Op = "mov"_op,
                    .Dest = dest,
                    .Operands = { TOperand(t) },
                    .OperandCount = 1,
                });
            }
            // Insert before the terminator, first all pre-copies then all post-copies
            auto termIt = predBlock.Instrs.end() - 1;
            predBlock.Instrs.insert(termIt, preCopies.begin(), preCopies.end());
            termIt = predBlock.Instrs.end() - 1; // iterator invalidated; recompute
            predBlock.Instrs.insert(termIt, postCopies.begin(), postCopies.end());
        }
        // Remove phis from the block after lowering
        block.Phis.clear();
    }
}

void DeSSA(TModule& module) {
    for (auto& function : module.Functions) {
        DeSSA(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir