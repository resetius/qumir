#pragma once

#include <expected>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

#include <qumir/error.h>
#include <qumir/optional.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/pragma.h>

namespace NQumir {
namespace NAst {
namespace NCore {

using TAstTask = TExpectedTask<TExprPtr, TError, TLocation>;

struct IParseHandle {
    virtual ~IParseHandle() = default;

    virtual TAstTask Expr() = 0;
    virtual TExpectedTask<TTypePtr, TError, TLocation> Type() = 0;
    virtual TToken Next() = 0;
    virtual void Unget(TToken) = 0;
    virtual TExpectedTask<std::monostate, TError, TLocation> Take(char op) = 0;

    static bool IsOp(const TToken& tok, char op) {
        return tok.Type == TToken::Operator && tok.Value.i64 == static_cast<int64_t>(op);
    }
    static TError MakeError(const TToken& tok, const std::string& msg) {
        return TError(tok.Location, msg);
    }
};

using TNodeParser = std::function<TAstTask(IParseHandle&, TLocation)>;
using TNodeParserMap = std::unordered_map<std::string, TNodeParser>;

class TParser {
public:
    TNodeParserMap NodeParsers;
    std::vector<TPragma> Pragmas;

    std::expected<TExprPtr, TError> Parse(TTokenStream& stream);
};

} // namespace NCore
} // namespace NAst
} // namespace NQumir
