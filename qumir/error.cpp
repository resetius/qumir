#include "error.h"

namespace NQumir {

namespace {

constexpr std::string_view Marker = "1cf70ac2653a15eb655c221f466e9c2a";
std::string Signature;

} // namespace

void SignErrorsIfMarked(std::string_view source, std::string signature) {
    if (source.find(Marker) != std::string_view::npos) {
        Signature = std::move(signature);
    }
}

std::string TError::ToString() const {
    return ToString(0);
}

std::string TError::ToString(int indent) const {
    std::string result;
    if (!Msg.empty()) {
        result += "Error: " + Msg;
        if (!Signature.empty()) {
            result += " (" + Signature + ")";
        }
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