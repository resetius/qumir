#include "cfg.h"

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void BuildCfg(TFunction& function)
{
    for (auto& block : function.Blocks) {
        block.Succ.clear();
        block.Pred.clear();
    }

    for (auto& block : function.Blocks) {
        if (block.Instrs.empty()) {
            throw std::runtime_error("Block has no instructions");
        }
        const auto& lastInstr = block.Instrs.back();
        switch (lastInstr.Op) {
            case "jmp"_op:
                if (lastInstr.OperandCount != 1) {
                    throw std::runtime_error("Jmp instruction must have exactly one operand");
                }
                if (lastInstr.Operands[0].Type != TOperand::EType::Label) {
                    throw std::runtime_error("Jmp instruction operand must be a label");
                }
                block.Succ.push_back(lastInstr.Operands[0].Label);
                break;
            case "cmp"_op:
                if (lastInstr.OperandCount != 3) {
                    throw std::runtime_error("Cmp instruction must have exactly three operands");
                }
                if (lastInstr.Operands[1].Type != TOperand::EType::Label ||
                    lastInstr.Operands[2].Type != TOperand::EType::Label)
                {
                    throw std::runtime_error("Cmp instruction operands must be labels");
                }
                block.Succ.push_back(lastInstr.Operands[1].Label);
                block.Succ.push_back(lastInstr.Operands[2].Label);
                break;
            case "ret"_op:
                break;
            default:
                throw std::runtime_error("Block does not end with a terminator instruction");
        }
    }

    for (auto& block : function.Blocks) {
        for (const auto& succLabel : block.Succ) {
            auto idx = succLabel.Idx;
            function.Blocks[idx].Pred.push_back(block.Label);
        }
    }
}

void BuildCfg(TModule& module) {
    for (auto& func : module.Functions) {
        BuildCfg(func);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir