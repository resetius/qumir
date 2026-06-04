#pragma once

#include <qumir/parser/type.h>

#include <optional>

namespace NQumir {

namespace NSemantics { class TNameResolver; }

namespace NTypeAnnotation {

// Cost of using an argument of type `from` where a parameter of type `to` is expected.
//   0       = exact match
//   1       = implicit coercion (widening integer, int->float, registered cast)
//   2       = implicit coercion with higher cost (int->float)
//   nullopt = not possible
std::optional<int> ArgCost(
    const NAst::TTypePtr& from,
    const NAst::TTypePtr& to,
    NSemantics::TNameResolver* ctx);

} // namespace NTypeAnnotation
} // namespace NQumir
