#pragma once

#include <qumir/semantics/name_resolution/name_resolver.h>

#include <cstddef>
#include <string>
#include <unordered_set>

namespace NQumir {
namespace NSemantics {

class TSyntheticNameGenerator {
public:
    explicit TSyntheticNameGenerator(
        const TNameResolver& context,
        const NAst::TExprPtr& root = nullptr);

    std::string Next();

private:
    std::unordered_set<std::string> ReservedNames;
    size_t NextId = 0;
};

} // namespace NSemantics
} // namespace NQumir
