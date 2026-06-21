#pragma once

#include <qumir/semantics/transform/transform.h>

namespace NQumir {
namespace NSemantics {
namespace NKumir {

std::expected<bool, TError> PowerTransform(
    NAst::TExprPtr& expr,
    TNameResolver& context);

NTransform::TPipelineExtensions PipelineExtensions();

} // namespace NKumir
} // namespace NSemantics
} // namespace NQumir
