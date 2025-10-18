#include "de_ssa.h"

#include <iostream>

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void DeSSA(TFunction& function, TModule& module) {
    for (auto& block : function.Blocks) {
        for (auto predIdx : block.Pred) {
            auto& pred = function.Blocks[predIdx.Idx];
            if (pred.Succ.size() > 1 && block.Pred.size() > 1) {
                // critical edge, need to split
                throw std::runtime_error("DeSSA: critical edge detected, splitting not implemented yet");
            }
        }
        for (const auto& phi : block.Phis) {
            if (phi.Op != "phi"_op) {
                // skip nops
                continue;
            }
            for (size_t i = 0; i < phi.Operands.size(); i += 2) {
                auto label = phi.Operands[i].Label;
                auto value = phi.Operands[i + 1];
                auto& pred = function.Blocks[label.Idx];
                auto op = (value.Type == TOperand::EType::Tmp)
                    ? "mov"_op
                    : "cmov"_op;
                TInstr assign {
                    .Op = op,
                    .Dest = phi.Dest,
                    .Operands = {value},
                    .OperandCount = 1,
                };
                if (pred.Instrs.empty()) {
                    throw std::runtime_error("DeSSA: cannot insert phi assignment into empty predecessor block: " + std::to_string(label.Idx));
                }
                pred.Instrs.insert(pred.Instrs.end()-1, assign);
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