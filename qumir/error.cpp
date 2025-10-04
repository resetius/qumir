#include "error.h"

namespace NQumir {

std::string TError::ToString() const {
    return ToString(0);
}

std::string TError::ToString(int indent) const {
    std::string result(indent, ' ');
    result += Msg + ": " + Location.ToString() + "\n";
    for (const auto& child : Children) {
        result += child.ToString(indent + 2);
    }
    return result;
}

} // namespace NQumir