#include "renumber_regs.h"

#include <unordered_map>

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void RenumberRegisters(TFunction& function, TModule& module) {
    std::unordered_map<int, int> tmpMapping;
    int nextTmpIdx = 0;

    auto getNewTmpIdx = [&](int oldIdx) {
        auto it = tmpMapping.find(oldIdx);
        if (it != tmpMapping.end()) {
            return it->second;
        }
        int newIdx = nextTmpIdx++;
        tmpMapping[oldIdx] = newIdx;
        return newIdx;
    };

    auto renumberInstr = [&](auto& instr) {
        if (instr.Op == "nop"_op) {
            return;
        }
        if (instr.Dest.Idx >= 0) {
            instr.Dest.Idx = getNewTmpIdx(instr.Dest.Idx);
        }
        for (int i = 0; i < instr.Size(); ++i) {
            auto& op = instr.Operands[i];
            if (op.Type == TOperand::EType::Tmp) {
                op.Tmp.Idx = getNewTmpIdx(op.Tmp.Idx);
            }
        }
    };

    for (auto& block : function.Blocks) {
        for (auto& phi : block.Phis) {
            renumberInstr(phi);
        }
        for (auto& instr : block.Instrs) {
            renumberInstr(instr);
        }
    }

    std::vector<int> newTmpTypes(nextTmpIdx, -1);
    for (const auto& [oldIdx, newIdx] : tmpMapping) {
        newTmpTypes[newIdx] = function.TmpTypes[oldIdx];
    }
    function.TmpTypes = std::move(newTmpTypes);
}

void RenumberRegisters(TModule& module) {
    for (auto& function : module.Functions) {
        RenumberRegisters(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir