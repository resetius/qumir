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

    for (auto& block : function.Blocks) {
        for (auto& phi : block.Phis) {
            if (phi.Op == "nop"_op) {
                continue;
            }
            phi.Dest.Idx = getNewTmpIdx(phi.Dest.Idx);
            for (auto& op : phi.Operands) {
                if (op.Type == TOperand::EType::Tmp) {
                    op.Tmp.Idx = getNewTmpIdx(op.Tmp.Idx);
                }
            }
        }
        for (auto& instr : block.Instrs) {
            if (instr.Op == "nop"_op) {
                continue;
            }
            if (instr.Dest.Idx >= 0) {
                instr.Dest.Idx = getNewTmpIdx(instr.Dest.Idx);
            }
            for (auto& op : instr.Operands) {
                if (op.Type == TOperand::EType::Tmp) {
                    op.Tmp.Idx = getNewTmpIdx(op.Tmp.Idx);
                }
            }
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