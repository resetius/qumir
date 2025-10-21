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
                    .Pred = {predLabel},
                    .Succ = {block.Label}
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
        for (const auto& phi : block.Phis) {
            if (phi.Op != "phi"_op) {
                // skip nops
                continue;
            }
            for (size_t i = 0; i < phi.Operands.size(); i += 2) {
                auto value = phi.Operands[i];
                auto label = phi.Operands[i + 1].Label;
                auto maybeRemapIt = remap.find({label, block.Label});
                if (maybeRemapIt != remap.end()) {
                    label = maybeRemapIt->second;;
                }
                auto& predBlock = function.Blocks[function.GetBlockIdx(label)];
                auto op = (value.Type == TOperand::EType::Tmp)
                    ? "mov"_op
                    : "cmov"_op;
                TInstr assign {
                    .Op = op,
                    .Dest = phi.Dest,
                    .Operands = {value},
                    .OperandCount = 1,
                };
                if (predBlock.Instrs.empty()) {
                    throw std::runtime_error("DeSSA: cannot insert phi assignment into empty predecessor block: " + std::to_string(label.Idx));
                }
                predBlock.Instrs.insert(predBlock.Instrs.end()-1, assign);
            }
        }
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