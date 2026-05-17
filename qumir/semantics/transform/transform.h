#pragma once

#include <qumir/parser/ast.h>
#include <qumir/error.h>

#include <qumir/semantics/name_resolution/name_resolver.h>

#include <expected>

namespace NQumir {
namespace NTransform {

std::expected<bool, TError> PreNameResolutionTransform(NAst::TExprPtr& expr);
std::expected<bool, TError> PostNameResolutionTransform(NAst::TExprPtr& expr, NSemantics::TNameResolver& r);
std::expected<bool, TError> PostTypeAnnotationTransform(NAst::TExprPtr& expr, NSemantics::TNameResolver& r);
std::expected<bool, TError> CoroutineAnnotationTransform(NAst::TExprPtr& expr, NSemantics::TNameResolver& r);

struct TPipelineOptions {
    bool EnableCoroutineAnalysis = false;
};

std::expected<std::monostate, TError> Pipeline(
    NAst::TExprPtr& expr,
    NSemantics::TNameResolver& context,
    TPipelineOptions options = {});

} // namespace NTransform
} // namespace NQumir
