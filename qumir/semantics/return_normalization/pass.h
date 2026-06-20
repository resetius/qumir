#pragma once

#include <qumir/error.h>
#include <qumir/parser/ast.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <expected>

namespace NQumir {
namespace NSemantics {

std::expected<bool, TError> ReturnNormalizationPass(
    NAst::TExprPtr& expr,
    TNameResolver& context);

} // namespace NSemantics
} // namespace NQumir
