#pragma once

#include <string>

namespace NQumir {

struct TLocation
{
    int Line{0};
    int Byte{0};
    int Column{0};

    std::string ToString() const {
        return "Line: " + std::to_string(Line) + ", Byte: " + std::to_string(Byte) + ", Column: " + std::to_string(Column);
    }
};

} // namespace NQumir