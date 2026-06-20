#pragma once

#include <qumir/error.h>
#include <qumir/parser/ast.h>
#include <qumir/semantics/lifetime/synthetic_name_generator.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <expected>

namespace NQumir {
namespace NSemantics {

std::expected<bool, TError> LifetimePass(
    NAst::TExprPtr& expr,
    TNameResolver& context,
    TSyntheticNameGenerator& syntheticNames);

} // namespace NSemantics
} // namespace NQumir
