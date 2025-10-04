#pragma once

#include <qumir/error.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/lexer.h>

#include <expected>

namespace NQumir {
namespace NAst {

class TParser {
public:
    std::expected<TExprPtr, TError> parse(TTokenStream& stream);
};

} // namespace NAst
} // namespace NQumir