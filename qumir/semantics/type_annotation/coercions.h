#pragma once

#include <qumir/parser/type.h>

#include <optional>
#include <tuple>
#include <unordered_set>

namespace NQumir {

namespace NSemantics { class TNameResolver; }

namespace NTypeAnnotation {

struct TArgCost {
    int GenericPenalty = 0;
    int TopLevelGenericPenalty = 0;
    int ConversionCost = 0;
    int FixedTypeNodes = 0;
    int GenericParamCount = 0;
};

inline TArgCost operator+(const TArgCost& left, const TArgCost& right) {
    return TArgCost{
        .GenericPenalty = left.GenericPenalty + right.GenericPenalty,
        .TopLevelGenericPenalty = left.TopLevelGenericPenalty + right.TopLevelGenericPenalty,
        .ConversionCost = left.ConversionCost + right.ConversionCost,
        .FixedTypeNodes = left.FixedTypeNodes + right.FixedTypeNodes,
        .GenericParamCount = left.GenericParamCount + right.GenericParamCount,
    };
}

inline bool operator<(const TArgCost& left, const TArgCost& right) {
    return std::tuple(
        left.GenericPenalty,
        left.TopLevelGenericPenalty,
        left.ConversionCost,
        -left.FixedTypeNodes,
        left.GenericParamCount)
        < std::tuple(
            right.GenericPenalty,
            right.TopLevelGenericPenalty,
            right.ConversionCost,
            -right.FixedTypeNodes,
            right.GenericParamCount);
}

inline bool operator==(const TArgCost& left, const TArgCost& right) {
    return left.GenericPenalty == right.GenericPenalty
        && left.TopLevelGenericPenalty == right.TopLevelGenericPenalty
        && left.ConversionCost == right.ConversionCost
        && left.FixedTypeNodes == right.FixedTypeNodes
        && left.GenericParamCount == right.GenericParamCount;
}

// Cost of using an argument of type `from` where a parameter of type `to` is expected.
// Concrete overloads are always cheaper than generic overloads, even when the
// concrete match needs a widening conversion.
std::optional<TArgCost> ArgCost(
    const NAst::TTypePtr& from,
    const NAst::TTypePtr& to,
    NSemantics::TNameResolver* ctx,
    const std::unordered_set<std::string>* genericTypeParams = nullptr,
    const std::unordered_set<std::string>* genericValueParams = nullptr);

} // namespace NTypeAnnotation
} // namespace NQumir
