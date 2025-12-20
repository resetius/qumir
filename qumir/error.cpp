#include "error.h"

namespace NQumir {

std::string TError::ToString() const {
    return ToString(0);
}

std::string TError::ToString(int indent) const {
    std::string result;
    if (!Msg.empty()) {
        result += "Error: " + Msg;
        if (Location) {
            result += " @ " + Location->ToString();
        }
        result += "\n";
    }
    for (const auto& child : Children) {
        result += child.ToString(indent + 2);
    }
    return result;
}

} // namespace NQumir