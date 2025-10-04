#include "ast.h"

namespace NQumir {
namespace NAst {

namespace {

void PrintSExpr(const TExpr& expr, std::ostream& os, int indent, int indentStep) {
    const auto& children = expr.Children();
    os << '(' << expr.ToString();

    if (expr.Type) {
        os << ':' << *expr.Type;
    }

    if (children.empty()) {
        os << ')';
        return;
    }

    os << '\n';
    for (size_t i = 0; i < children.size(); ++i) {
        os << std::string(static_cast<size_t>(indent + indentStep), ' ');
        if (!children[i]) {
            os << "nil";
        } else {
            PrintSExpr(*children[i], os, indent + indentStep, indentStep);
        }
        if (i + 1 < children.size()) {
            os << '\n';
        }
    }
    os << ')';
}

} // namespace

std::ostream& operator<<(std::ostream& os, const TExpr& expr) {
    PrintSExpr(expr, os, /*indent=*/0, /*indentStep=*/2);
    return os;
}

} // namespace NAst
} // namespace NQumir
