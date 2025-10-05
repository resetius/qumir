#include "type.h"

#include <iostream>
#include <sstream>

namespace NQumir {
namespace NAst {

namespace {

std::string Print(const TType& expr) {
    std::string result = "<";
    result += expr.TypeName();
    std::string tail = expr.ToString();
    if (!tail.empty()) {
        result += " ";
        result += tail;
    }
    result += ">";
    return result;
}

} // namespace

std::ostream& operator<<(std::ostream& os, const TType& expr)
{
    os << Print(expr);
    return os;
}

} // namespace NAst
} // namespace NQumir