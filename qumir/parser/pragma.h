#pragma once

#include <qumir/location.h>

#include <string>
#include <vector>

namespace NQumir {
namespace NAst {

struct TPragma {
    std::string Group;
    std::vector<std::string> Values;
    TLocation Location;
};

} // namespace NAst
} // namespace NQumir
