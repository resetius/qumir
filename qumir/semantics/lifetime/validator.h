#pragma once

#include <qumir/error.h>
#include <qumir/parser/ast.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <expected>

namespace NQumir {
namespace NSemantics {

class TLifetimeValidator {
public:
    explicit TLifetimeValidator(TNameResolver& context);

    std::expected<void, TError> Validate(const NAst::TExprPtr& root);

private:
    TNameResolver& Context_;
};

} // namespace NSemantics
} // namespace NQumir
