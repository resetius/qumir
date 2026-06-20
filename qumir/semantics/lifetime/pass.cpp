#include "pass.h"

namespace NQumir {
namespace NSemantics {

std::expected<bool, TError> LifetimePass(
    NAst::TExprPtr& expr,
    TNameResolver& context,
    TSyntheticNameGenerator& syntheticNames)
{
    (void)expr;
    (void)context;
    (void)syntheticNames;
    return false;
}

} // namespace NSemantics
} // namespace NQumir
