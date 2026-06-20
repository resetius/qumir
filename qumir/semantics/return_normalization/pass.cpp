#include "pass.h"

namespace NQumir {
namespace NSemantics {

std::expected<bool, TError> ReturnNormalizationPass(
    NAst::TExprPtr& expr,
    TNameResolver& context)
{
    (void)expr;
    (void)context;
    return false;
}

} // namespace NSemantics
} // namespace NQumir
