#include "const_fold.h"

#include <unordered_map>

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void ConstFold(TFunction& function, TModule& module) {
    std::unordered_map<int, std::vector<TInstr*>> users; // tmp idx -> instr
    std::unordered_map<int, std::vector<TPhi*>> phiUsers; // tmp idx -> phi

    auto calcUsers = [&]() {
        for (auto& block : function.Blocks) {
            for (auto& instr : block.Instrs) {
                for (int i = 0; i < instr.Size(); ++i) {
                    const auto& op = instr.Operands[i];
                    if (op.Type == TOperand::EType::Tmp) {
                        users[op.Tmp.Idx].push_back(&instr);
                    }
                }
            }
            for (auto& phi : block.Phis) {
                for (int i = 0; i < phi.Size(); ++i) {
                    const auto& op = phi.Operands[i];
                    if (op.Type == TOperand::EType::Tmp) {
                        phiUsers[op.Tmp.Idx].push_back(&phi);
                    }
                }
            }
        }
    };

    calcUsers();

    auto replaceTmpWithOp = [&](TTmp tmp, TOperand imm) {
        // Replace all uses of tmp with imm
        if (users.count(tmp.Idx)) {
            for (auto instr : users[tmp.Idx]) {
                for (int i = 0; i < instr->Size(); ++i) {
                    auto& op = instr->Operands[i];
                    if (op == tmp) {
                        op = imm;
                    }
                }
            }
            users.erase(tmp.Idx);
        }
        if (phiUsers.count(tmp.Idx)) {
            for (auto phi : phiUsers[tmp.Idx]) {
                for (int i = 0; i < phi->Size(); ++i) {
                    auto& op = phi->Operands[i];
                    if (op == tmp) {
                        op = imm;
                    }
                }
            }
            phiUsers.erase(tmp.Idx);
        }
    };

    auto applyBinOp = [&](TOp op, auto v1, auto v2) -> std::optional<decltype(v1)> {
        if (op == "+"_op) return v1 + v2;
        if (op == "-"_op) return v1 - v2;
        if (op == "*"_op) return v1 * v2;
        if (op == "/"_op) {
            if (v2 == 0) return std::nullopt;
            return v1 / v2;
        }
        return std::nullopt;
    };

    auto processInstr = [&](TInstr& instr) -> bool {
        auto& op = instr.Op;
        if (!(op == "+"_op || op == "-"_op ||
            op == "*"_op || op == "/"_op))
        {
            return false;
        }

        if (instr.Operands[0].Type == TOperand::EType::Imm &&
            instr.Operands[1].Type == TOperand::EType::Imm)
        {
            if (module.Types.IsInteger(instr.Operands[0].Imm.TypeId) &&
                module.Types.IsInteger(instr.Operands[1].Imm.TypeId))
            {
                auto v1 = instr.Operands[0].Imm.Value;
                auto v2 = instr.Operands[1].Imm.Value;
                int64_t v;
                if (auto result = applyBinOp(op, v1, v2)) {
                    v = *result;
                } else {
                    return false;
                }
                replaceTmpWithOp(instr.Dest, TImm{v, instr.Operands[0].Imm.TypeId});
                instr.Clear();
                return true;
            }
            if (module.Types.IsFloat(instr.Operands[0].Imm.TypeId) &&
                module.Types.IsFloat(instr.Operands[1].Imm.TypeId))
            {
                double v1 = std::bit_cast<double>(instr.Operands[0].Imm.Value);
                double v2 = std::bit_cast<double>(instr.Operands[1].Imm.Value);
                double v;
                if (auto result = applyBinOp(op, v1, v2)) {
                    v = *result;
                } else {
                    return false;
                }
                replaceTmpWithOp(instr.Dest, TImm{std::bit_cast<int64_t>(v), instr.Operands[0].Imm.TypeId});
                instr.Clear();
                return true;
            }
        }
        if (instr.Operands[0].Type == TOperand::EType::Tmp &&
            instr.Operands[1].Type == TOperand::EType::Imm)
        {
            if (instr.Operands[1].Imm.Value == 0) {
                // x + 0 = x, x - 0 = x, x * 0 = 0, x / 0 = undef
                if (op == "+"_op || op == "-"_op) {
                    replaceTmpWithOp(instr.Dest, instr.Operands[0]);
                    instr.Clear();
                    return true;
                }
                if (op == "*"_op) {
                    replaceTmpWithOp(instr.Dest, TImm{0, instr.Operands[0].Imm.TypeId});
                    instr.Clear();
                    return true;
                }
            }
            if (module.Types.IsInteger(instr.Operands[1].Imm.Value) && instr.Operands[1].Imm.Value == 1) {
                // x * 1 = x, x / 1 = x
                if (op == "*"_op || op == "/"_op) {
                    replaceTmpWithOp(instr.Dest, instr.Operands[0]);
                    instr.Clear();
                    return true;
                }
            }
        }
        if (instr.Operands[0].Type == TOperand::EType::Imm &&
            instr.Operands[1].Type == TOperand::EType::Tmp)
        {
            if (instr.Operands[0].Imm.Value == 0) {
                // 0 + x = x, 0 - x = -x, 0 * x = 0, 0 / x = 0
                if (op == "+"_op) {
                    replaceTmpWithOp(instr.Dest, instr.Operands[1]);
                    instr.Clear();
                    return true;
                }
                if (op == "*"_op) {
                    replaceTmpWithOp(instr.Dest, TImm{0, instr.Operands[0].Imm.TypeId});
                    instr.Clear();
                    return true;
                }
                if (op == "/"_op) {
                    replaceTmpWithOp(instr.Dest, TImm{0, instr.Operands[0].Imm.TypeId});
                    instr.Clear();
                    return true;
                }
            }
            if (module.Types.IsInteger(instr.Operands[0].Imm.Value) && instr.Operands[0].Imm.Value == 1) {
                // 1 * x = x
                if (op == "*"_op) {
                    replaceTmpWithOp(instr.Dest, instr.Operands[1]);
                    instr.Clear();
                    return true;
                }
            }
        }

        return false;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& block : function.Blocks) {
            for (auto& instr : block.Instrs) {
                if (instr.Op == "nop"_op) continue;
                if (instr.OperandCount == 0) continue;
                changed |= processInstr(instr);
            }
        }
    }
}

void ConstFold(TModule& module) {
    for (auto& function : module.Functions) {
        ConstFold(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir