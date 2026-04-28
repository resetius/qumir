#pragma once

#include <qumir/error.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/lexer.h>
#include <qumir/module_manager.h>

#include <expected>

namespace NQumir {
namespace NAst {

class TParser {
public:
    // moduleManager: if non-null, `использовать X` imports X at parse time;
    // type names from imported modules are registered in the stream's TLexerContext
    std::expected<TExprPtr, TError> parse(TTokenStream& stream, IModuleManager* moduleManager = nullptr);
};

} // namespace NAst
} // namespace NQumir