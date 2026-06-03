#include "parser.h"

#include <qumir/optional.h>
#include <qumir/parser/operator.h>
#include <qumir/parser/type.h>

#include <string>
#include <vector>

namespace NQumir {
namespace NAst {
namespace NCore {

namespace {

using TAstTask = TExpectedTask<TExprPtr, TError, TLocation>;
using TTypeTask = TExpectedTask<TTypePtr, TError, TLocation>;
using TExprListTask = TExpectedTask<std::vector<TExprPtr>, TError, TLocation>;
using TTypeListTask = TExpectedTask<std::vector<TTypePtr>, TError, TLocation>;
using TOutputArgTask = TExpectedTask<TOutputArg, TError, TLocation>;
using TOutputArgListTask = TExpectedTask<std::vector<TOutputArg>, TError, TLocation>;
using TBindingListTask = TExpectedTask<std::vector<TLetExpr::TBinding>, TError, TLocation>;
using TParamListTask = TExpectedTask<std::vector<TParam>, TError, TLocation>;
using TBoundsTask = TExpectedTask<std::vector<std::pair<TExprPtr, TExprPtr>>, TError, TLocation>;

struct TParserContext;

using TListHandler = std::function<TAstTask(TParserContext&, TLocation)>;
using TListHandlerMap = std::unordered_map<std::string, TListHandler>;

struct TParserContext {
    TWrappedTokenStream& Stream;
    TListHandlerMap Handlers;
};

std::shared_ptr<TIntegerType> IntegerTypeByName(const std::string& name) {
    using K = TIntegerType::EKind;
    if (name == "i8") return std::make_shared<TIntegerType>(K::I8);
    if (name == "i16") return std::make_shared<TIntegerType>(K::I16);
    if (name == "i32") return std::make_shared<TIntegerType>(K::I32);
    if (name == "i64") return std::make_shared<TIntegerType>(K::I64);
    if (name == "u8") return std::make_shared<TIntegerType>(K::U8);
    if (name == "u16") return std::make_shared<TIntegerType>(K::U16);
    if (name == "u32") return std::make_shared<TIntegerType>(K::U32);
    if (name == "u64") return std::make_shared<TIntegerType>(K::U64);
    return nullptr;
}

bool IsEof(const TToken& token) {
    return token.Type == TToken::Operator && token.Value.i64 == -1;
}

bool IsOp(const TToken& token, char op) {
    return token.Type == TToken::Operator && token.Value.i64 == static_cast<int64_t>(op);
}

bool IsNil(const TToken& token) {
    return token.Type == TToken::Identifier && token.Name == "nil";
}

std::string TokenText(const TToken& token) {
    if (token.Type == TToken::Identifier || token.Type == TToken::String) {
        return token.Name;
    }
    if (token.Type == TToken::Operator) {
        return TOperator(static_cast<uint64_t>(token.Value.i64)).ToString();
    }
    return token.RawValue;
}

TError Error(const TToken& token, const std::string& message) {
    return TError(token.Location, message);
}

TExpectedTask<std::monostate, TError, TLocation> Expect(TParserContext& context, char op);

TExpectedTask<std::string, TError, TLocation> ParseName(TParserContext& context) {
    auto token = context.Stream.Next();
    if (IsOp(token, '<')) {
        auto inner = context.Stream.Next();
        if (inner.Type != TToken::Identifier) {
            co_return Error(inner, "expected angle-bracket identifier");
        }
        co_await Expect(context, '>');
        co_return "<" + inner.Name + ">";
    }
    if (token.Type != TToken::Identifier) {
        co_return Error(token, "expected identifier");
    }
    co_return token.Name;
}

TExpectedTask<std::string, TError, TLocation> ParseNameLike(TParserContext& context) {
    auto token = context.Stream.Next();
    if (token.Type != TToken::Identifier && token.Type != TToken::String) {
        co_return Error(token, "expected name");
    }
    co_return token.Name;
}

TExpectedTask<std::monostate, TError, TLocation> Expect(TParserContext& context, char op) {
    auto token = context.Stream.Next();
    if (!IsOp(token, op)) {
        co_return Error(token, std::string("expected '") + op + "'");
    }
    co_return std::monostate{};
}

TAstTask ParseExpr(TParserContext& context);
TTypeTask ParseType(TParserContext& context);

TExprPtr ApplyTypeAnnotation(TExprPtr expr, TTypePtr type) {
    if (auto num = TMaybeNode<TNumberExpr>(expr)) {
        if (TMaybeType<TFloatType>(type) && !num.Cast()->IsFloat) {
            expr = std::make_shared<TNumberExpr>(expr->Location, static_cast<double>(num.Cast()->IntValue));
        } else if (TMaybeType<TIntegerType>(type) && num.Cast()->IsFloat) {
            expr = std::make_shared<TNumberExpr>(expr->Location, static_cast<int64_t>(num.Cast()->FloatValue));
        } else if (TMaybeType<TBoolType>(type)) {
            expr = std::make_shared<TNumberExpr>(expr->Location, num.Cast()->IsFloat ? num.Cast()->FloatValue != 0.0 : num.Cast()->IntValue != 0);
        } else if (TMaybeType<TSymbolType>(type)) {
            expr = std::make_shared<TNumberExpr>(expr->Location, num.Cast()->IntValue);
        }
    }
    expr->Type = std::move(type);
    return expr;
}

TExprListTask ParseExprsUntil(TParserContext& context, char close) {
    std::vector<TExprPtr> result;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, close)) {
            co_return result;
        }
        if (IsEof(token)) {
            co_return Error(token, std::string("expected '") + close + "'");
        }
        context.Stream.Unget(token);
        result.push_back(co_await ParseExpr(context));
    }
}

TTypeListTask ParseTypeList(TParserContext& context) {
    co_await Expect(context, '(');
    std::vector<TTypePtr> result;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, ')')) {
            co_return result;
        }
        if (IsEof(token)) {
            co_return Error(token, "expected ')' in type list");
        }
        context.Stream.Unget(token);
        result.push_back(co_await ParseType(context));
    }
}

TBoundsTask ParseBounds(TParserContext& context) {
    std::vector<std::pair<TExprPtr, TExprPtr>> result;
    while (true) {
        auto token = context.Stream.Next();
        if (!IsOp(token, '[')) {
            context.Stream.Unget(token);
            co_return result;
        }
        auto from = co_await ParseExpr(context);
        auto to = co_await ParseExpr(context);
        co_await Expect(context, ']');
        result.emplace_back(std::move(from), std::move(to));
    }
}

TExprListTask ParseIndexVector(TParserContext& context) {
    co_await Expect(context, '[');
    co_return co_await ParseExprsUntil(context, ']');
}

TBindingListTask ParseBindings(TParserContext& context) {
    co_await Expect(context, '(');
    std::vector<TLetExpr::TBinding> result;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, ')')) {
            co_return result;
        }
        if (!IsOp(token, '(')) {
            co_return Error(token, "expected binding");
        }
        auto name = co_await ParseName(context);
        auto value = co_await ParseExpr(context);
        co_await Expect(context, ')');
        result.push_back(TLetExpr::TBinding{
            .Name = std::move(name),
            .Value = std::move(value),
        });
    }
}

TParamListTask ParseParams(TParserContext& context) {
    co_await Expect(context, '(');
    std::vector<TParam> result;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, ')')) {
            co_return result;
        }
        context.Stream.Unget(token);
        auto param = co_await ParseExpr(context);
        auto var = TMaybeNode<TVarStmt>(param);
        if (!var) {
            co_return TError(param ? param->Location : context.Stream.GetLocation(), "expected parameter var");
        }
        result.push_back(var.Cast());
    }
}

TOutputArgTask ParseOutputArg(TParserContext& context) {
    auto token = context.Stream.Next();
    if (IsOp(token, '(')) {
        auto head = context.Stream.Next();
        if (head.Type == TToken::Identifier && head.Name == "fmt") {
            auto expr = co_await ParseExpr(context);
            auto width = co_await ParseExpr(context);
            TExprPtr precision;
            auto t = context.Stream.Next();
            if (!IsOp(t, ')')) {
                context.Stream.Unget(t);
                precision = co_await ParseExpr(context);
                co_await Expect(context, ')');
            }
            co_return TOutputArg{
                .Expr = std::move(expr),
                .Width = std::move(width),
                .Precision = std::move(precision),
            };
        }
        context.Stream.Unget(head);
        context.Stream.Unget(token);
    } else {
        context.Stream.Unget(token);
    }
    co_return TOutputArg{.Expr = co_await ParseExpr(context)};
}

TOutputArgListTask ParseOutputArgs(TParserContext& context) {
    std::vector<TOutputArg> result;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, ')')) {
            co_return result;
        }
        context.Stream.Unget(token);
        result.push_back(co_await ParseOutputArg(context));
    }
}

TExpectedTask<std::monostate, TError, TLocation> SkipBalancedList(TParserContext& context) {
    co_await Expect(context, '(');
    int depth = 1;
    while (depth > 0) {
        auto token = context.Stream.Next();
        if (IsEof(token)) {
            co_return Error(token, "unexpected eof in list");
        }
        if (IsOp(token, '(')) {
            ++depth;
        } else if (IsOp(token, ')')) {
            --depth;
        }
    }
    co_return std::monostate{};
}

struct TFunAttrs {
    TExprPtr After;  // LastAssert
    TExprPtr Before; // pre-condition (future)
};

TExpectedTask<TFunAttrs, TError, TLocation> ParseFunAttrs(TParserContext& context) {
    co_await Expect(context, '(');
    TFunAttrs attrs;
    while (true) {
        auto token = context.Stream.Next();
        if (IsOp(token, ')')) {
            break;
        }
        if (IsOp(token, '(')) {
            // compound attr: (name expr)
            auto name = co_await ParseName(context);
            auto expr = co_await ParseExpr(context);
            co_await Expect(context, ')');
            if (name == "expect_after") {
                attrs.After = std::move(expr);
            } else if (name == "expect_before") {
                attrs.Before = std::move(expr);
            }
            // unknown attrs silently ignored for forward compat
        } else if (token.Type == TToken::Identifier) {
            // simple attr: inline, etc. — ignored for now
        } else {
            co_return Error(token, "expected function attribute");
        }
    }
    co_return attrs;
}

TAstTask ParseList(TParserContext& context, TLocation location) {
    auto headToken = context.Stream.Next();
    if (headToken.Type != TToken::Identifier && headToken.Type != TToken::Operator) {
        co_return Error(headToken, "expected form name");
    }
    const auto head = TokenText(headToken);

    if (auto it = context.Handlers.find(head); it != context.Handlers.end()) {
        co_return co_await it->second(context, location);
    }

    auto args = co_await ParseExprsUntil(context, ')');
    if (args.size() == 1) {
        co_return std::make_shared<TUnaryExpr>(location, TOperator(head), std::move(args[0]));
    }
    if (args.size() == 2) {
        co_return std::make_shared<TBinaryExpr>(location, TOperator(head), std::move(args[0]), std::move(args[1]));
    }
    co_return TError(location, "unknown core form: " + head);
}

TAstTask ParseExpr(TParserContext& context) {
    auto token = context.Stream.Next();
    if (IsNil(token)) {
        co_return TExprPtr{};
    }
    if (token.Type == TToken::Integer) {
        co_return std::make_shared<TNumberExpr>(token.Location, token.Value.i64);
    }
    if (token.Type == TToken::Float) {
        co_return std::make_shared<TNumberExpr>(token.Location, token.Value.f64);
    }
    if (token.Type == TToken::Boolean) {
        co_return std::make_shared<TNumberExpr>(token.Location, token.Value.i64 != 0);
    }
    if (token.Type == TToken::Char) {
        auto expr = std::make_shared<TNumberExpr>(token.Location, token.Value.i64);
        expr->Type = std::make_shared<TSymbolType>();
        co_return expr;
    }
    if (token.Type == TToken::String) {
        co_return std::make_shared<TStringLiteralExpr>(token.Location, token.Name);
    }
    if (token.Type == TToken::Identifier) {
        if (token.Name == "break") co_return std::make_shared<TBreakStmt>(token.Location);
        if (token.Name == "continue") co_return std::make_shared<TContinueStmt>(token.Location);
        co_return std::make_shared<TIdentExpr>(token.Location, token.Name);
    }
    if (IsOp(token, '(')) {
        co_return co_await ParseList(context, token.Location);
    }
    co_return Error(token, "expected expression");
}

// Reads optional "(readable mutable)" attrs and applies them to type.
TExpectedTask<std::monostate, TError, TLocation> ParseTypeAttrs(TParserContext& context, TType& type) {
    auto token = context.Stream.Next();
    if (!IsOp(token, '(')) {
        context.Stream.Unget(token);
        co_return std::monostate{};
    }
    type.Readable = false;
    type.Mutable = false;
    while (true) {
        token = context.Stream.Next();
        if (IsOp(token, ')')) {
            break;
        }
        if (token.Type != TToken::Identifier) {
            co_return Error(token, "expected type attribute");
        }
        if (token.Name == "readable") {
            type.Readable = true;
        } else if (token.Name == "mutable") {
            type.Mutable = true;
        } else {
            co_return Error(token, "unknown type attribute: " + token.Name);
        }
    }
    co_return std::monostate{};
}

TTypeTask ParseCompositeType(TParserContext& context, TLocation location) {
    auto head = co_await ParseName(context);

    // Scalar types wrapped in angle brackets: <i64 (mutable)> etc.
    {
        TTypePtr scalar;
        if (auto integerType = IntegerTypeByName(head)) {
            scalar = std::move(integerType);
        } else if (head == "f64") {
            scalar = std::make_shared<TFloatType>();
        } else if (head == "bool") {
            scalar = std::make_shared<TBoolType>();
        } else if (head == "string") {
            scalar = std::make_shared<TStringType>();
        } else if (head == "char") {
            scalar = std::make_shared<TSymbolType>();
        } else if (head == "file") {
            scalar = std::make_shared<TFileType>();
        } else if (head == "void") {
            scalar = std::make_shared<TVoidType>();
        }
        if (scalar) {
            co_await ParseTypeAttrs(context, *scalar);
            co_await Expect(context, '>');
            co_return scalar;
        }
    }

    if (head == "fun") {
        auto returnType = co_await ParseType(context);
        auto params = co_await ParseTypeList(context);
        while (true) {
            auto token = context.Stream.Next();
            if (IsOp(token, '>')) break;
            if (IsEof(token)) co_return Error(token, "expected '>'");
        }
        co_return std::make_shared<TFunctionType>(std::move(params), std::move(returnType));
    }
    if (head == "future") {
        auto resultType = co_await ParseType(context);
        auto type = std::make_shared<TFutureType>(std::move(resultType));
        co_await ParseTypeAttrs(context, *type);
        co_await Expect(context, '>');
        co_return type;
    }
    if (head == "array") {
        auto elementType = co_await ParseType(context);
        auto arity = context.Stream.Next();
        if (arity.Type != TToken::Integer) co_return Error(arity, "expected array arity");
        co_await Expect(context, '>');
        co_return std::make_shared<TArrayType>(std::move(elementType), static_cast<int>(arity.Value.i64));
    }
    if (head == "ptr") {
        auto pointee = co_await ParseType(context);
        auto type = std::make_shared<TPointerType>(std::move(pointee));
        co_await ParseTypeAttrs(context, *type);
        co_await Expect(context, '>');
        co_return type;
    }
    if (head == "ref") {
        auto referenced = co_await ParseType(context);
        auto type = std::make_shared<TReferenceType>(std::move(referenced));
        co_await ParseTypeAttrs(context, *type);
        co_await Expect(context, '>');
        co_return type;
    }
    if (head == "named") {
        auto name = co_await ParseName(context);
        auto underlying = co_await ParseType(context);
        auto type = std::make_shared<TNamedType>(std::move(name), std::move(underlying));
        co_await ParseTypeAttrs(context, *type);
        co_await Expect(context, '>');
        co_return type;
    }
    if (head == "struct") {
        std::vector<std::pair<std::string, TTypePtr>> fields;
        while (true) {
            auto token = context.Stream.Next();
            if (IsOp(token, '>')) break;
            if (!IsOp(token, '(')) co_return Error(token, "expected struct field type");
            auto name = co_await ParseName(context);
            auto type = co_await ParseType(context);
            co_await Expect(context, ')');
            fields.emplace_back(std::move(name), std::move(type));
        }
        co_return std::make_shared<TStructType>(std::move(fields));
    }
    co_return TError(location, "unknown composite type: " + head);
}

TTypeTask ParseType(TParserContext& context) {
    auto token = context.Stream.Next();
    if (IsNil(token)) co_return TTypePtr{};
    if (token.Type == TToken::Identifier) {
        if (auto integerType = IntegerTypeByName(token.Name)) co_return integerType;
        if (token.Name == "f64") co_return std::make_shared<TFloatType>();
        if (token.Name == "bool") co_return std::make_shared<TBoolType>();
        if (token.Name == "string") co_return std::make_shared<TStringType>();
        if (token.Name == "char") co_return std::make_shared<TSymbolType>();
        if (token.Name == "file") co_return std::make_shared<TFileType>();
        if (token.Name == "void") co_return std::make_shared<TVoidType>();
        co_return std::make_shared<TNamedType>(token.Name, nullptr);
    }
    if (IsOp(token, '<')) {
        co_return co_await ParseCompositeType(context, token.Location);
    }
    co_return Error(token, "expected type");
}

TListHandlerMap MakeDefaultHandlers() {
    return TListHandlerMap{
        {":", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto expr = co_await ParseExpr(ctx);
            auto type = co_await ParseType(ctx);
            expr = ApplyTypeAnnotation(std::move(expr), std::move(type));
            co_await Expect(ctx, ')');
            co_return expr;
        }},
        {"=", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto name = co_await ParseName(ctx);
            auto token = ctx.Stream.Next();
            if (IsOp(token, '[')) {
                ctx.Stream.Unget(token);
                auto indices = co_await ParseIndexVector(ctx);
                auto value = co_await ParseExpr(ctx);
                co_await Expect(ctx, ')');
                co_return std::make_shared<TArrayAssignExpr>(loc, std::move(name), std::move(indices), std::move(value));
            }
            ctx.Stream.Unget(token);
            auto value = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TAssignExpr>(loc, std::move(name), std::move(value));
        }},
        {"block", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            co_return std::make_shared<TBlockExpr>(loc, co_await ParseExprsUntil(ctx, ')'));
        }},
        {"seq", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            co_return std::make_shared<TSeqExpr>(loc, co_await ParseExprsUntil(ctx, ')'));
        }},
        {"vars", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            co_return std::make_shared<TVarsBlockExpr>(loc, co_await ParseExprsUntil(ctx, ')'));
        }},
        {"input", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            co_return std::make_shared<TInputExpr>(loc, co_await ParseExprsUntil(ctx, ')'));
        }},
        {"cond", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto cond = co_await ParseExpr(ctx);
            auto thenBranch = co_await ParseExpr(ctx);
            TExprPtr elseBranch;
            auto token = ctx.Stream.Next();
            if (!IsOp(token, ')')) {
                ctx.Stream.Unget(token);
                elseBranch = co_await ParseExpr(ctx);
                co_await Expect(ctx, ')');
            }
            co_return std::make_shared<TIfStmt>(loc, std::move(cond), std::move(thenBranch), std::move(elseBranch));
        }},
        {"if", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto cond = co_await ParseExpr(ctx);
            auto thenBranch = co_await ParseExpr(ctx);
            TExprPtr elseBranch;
            auto token = ctx.Stream.Next();
            if (!IsOp(token, ')')) {
                ctx.Stream.Unget(token);
                elseBranch = co_await ParseExpr(ctx);
                co_await Expect(ctx, ')');
            }
            co_return std::make_shared<TIfExpr>(loc, std::move(cond), std::move(thenBranch), std::move(elseBranch));
        }},
        {"let", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto bindings = co_await ParseBindings(ctx);
            auto body = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TLetExpr>(loc, std::move(bindings), std::move(body));
        }},
        {"while", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto cond = co_await ParseExpr(ctx);
            auto body = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TWhileStmtExpr>(loc, std::move(cond), std::move(body));
        }},
        {"repeat", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto body = co_await ParseExpr(ctx);
            auto cond = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TRepeatStmtExpr>(loc, std::move(body), std::move(cond));
        }},
        {"for", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto name = co_await ParseName(ctx);
            auto from = co_await ParseExpr(ctx);
            auto to = co_await ParseExpr(ctx);
            auto step = co_await ParseExpr(ctx);
            auto body = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TForStmtExpr>(loc, std::move(name), std::move(from), std::move(to), std::move(step), std::move(body));
        }},
        {"times", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto count = co_await ParseExpr(ctx);
            auto body = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TTimesStmtExpr>(loc, std::move(count), std::move(body));
        }},
        {"var", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto name = co_await ParseName(ctx);
            auto type = co_await ParseType(ctx);
            auto bounds = co_await ParseBounds(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TVarStmt>(loc, std::move(name), std::move(type), std::move(bounds));
        }},
        {"fun", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto name = co_await ParseName(ctx);
            auto returnType = co_await ParseType(ctx);
            auto params = co_await ParseParams(ctx);
            auto attrs = co_await ParseFunAttrs(ctx);
            auto bodyExpr = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            auto body = TMaybeNode<TBlockExpr>(bodyExpr).Cast();
            if (!body) {
                co_return TError(bodyExpr ? bodyExpr->Location : loc, "function body must be block");
            }
            auto funDecl = std::make_shared<TFunDecl>(loc, std::move(name), std::move(params), std::move(body), std::move(returnType));
            funDecl->LastAssert = std::move(attrs.After);
            std::vector<TTypePtr> paramTypes;
            for (const auto& param : funDecl->Params) {
                paramTypes.push_back(param->Type);
            }
            funDecl->Type = std::make_shared<TFunctionType>(std::move(paramTypes), funDecl->RetType);
            co_return funDecl;
        }},
        {"call", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto callee = co_await ParseExpr(ctx);
            auto args = co_await ParseExprsUntil(ctx, ')');
            co_return std::make_shared<TCallExpr>(loc, std::move(callee), std::move(args));
        }},
        {"await", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto operand = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TAwaitExpr>(loc, std::move(operand));
        }},
        {"output", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto args = co_await ParseOutputArgs(ctx);
            co_return std::make_shared<TOutputExpr>(loc, std::move(args));
        }},
        {"cast", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto operand = co_await ParseExpr(ctx);
            auto type = co_await ParseType(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TCastExpr>(loc, std::move(operand), std::move(type));
        }},
        {"index", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto token = ctx.Stream.Next();
            if (IsOp(token, '[')) {
                ctx.Stream.Unget(token);
                auto indices = co_await ParseIndexVector(ctx);
                auto collection = co_await ParseExpr(ctx);
                co_await Expect(ctx, ')');
                co_return std::make_shared<TMultiIndexExpr>(loc, std::move(collection), std::move(indices));
            }
            ctx.Stream.Unget(token);
            auto index = co_await ParseExpr(ctx);
            auto collection = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TIndexExpr>(loc, std::move(collection), std::move(index));
        }},
        {"slice", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto bounds = co_await ParseIndexVector(ctx);
            if (bounds.empty() || bounds.size() > 2) {
                co_return TError(loc, "slice expects one or two bounds");
            }
            auto collection = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TSliceExpr>(loc, std::move(collection), std::move(bounds[0]), bounds.size() == 2 ? std::move(bounds[1]) : nullptr);
        }},
        {"use", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto moduleName = co_await ParseNameLike(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TUseExpr>(loc, std::move(moduleName));
        }},
        {"assert", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto expr = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TAssertStmt>(loc, std::move(expr));
        }},
        {"field", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto fieldName = co_await ParseName(ctx);
            auto object = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TFieldAccessExpr>(loc, std::move(object), std::move(fieldName));
        }},
        {"struct", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            co_await Expect(ctx, '(');
            std::vector<std::pair<std::string, TTypePtr>> fieldTypes;
            std::vector<TExprPtr> fields;
            while (true) {
                auto token = ctx.Stream.Next();
                if (IsOp(token, ')')) break;
                if (!IsOp(token, '(')) co_return Error(token, "expected struct field");
                auto name = co_await ParseName(ctx);
                fields.push_back(co_await ParseExpr(ctx));
                fieldTypes.emplace_back(std::move(name), nullptr);
                co_await Expect(ctx, ')');
            }
            co_await Expect(ctx, ')');
            co_return std::make_shared<TStructConstructExpr>(loc, std::make_shared<TStructType>(std::move(fieldTypes)), std::move(fields));
        }},
        {"field_assign", [](TParserContext& ctx, TLocation loc) -> TAstTask {
            auto object = co_await ParseExpr(ctx);
            auto fieldName = co_await ParseName(ctx);
            auto value = co_await ParseExpr(ctx);
            co_await Expect(ctx, ')');
            co_return std::make_shared<TFieldAssignExpr>(loc, std::move(object), std::move(fieldName), std::move(value));
        }},
    };
}

struct TParseHandleImpl : NQumir::NAst::NCore::IParseHandle {
    TParserContext& Ctx;
    explicit TParseHandleImpl(TParserContext& ctx) : Ctx(ctx) {}

    TAstTask Expr() override {
        co_return co_await ParseExpr(Ctx);
    }
    TExpectedTask<TTypePtr, TError, TLocation> Type() override {
        co_return co_await ParseType(Ctx);
    }
    TToken Next() override {
        return Ctx.Stream.Next();
    }
    void Unget(TToken tok) override {
        Ctx.Stream.Unget(std::move(tok));
    }
    TExpectedTask<std::monostate, TError, TLocation> Take(char op) override {
        co_return co_await Expect(Ctx, op);
    }
};

} // namespace

std::expected<TExprPtr, TError> TParser::Parse(TTokenStream& baseStream) {
    TWrappedTokenStream stream(baseStream, 4);
    TParserContext context{stream, MakeDefaultHandlers()};
    for (auto& [key, handler] : NodeParsers) {
        context.Handlers[key] = [h = handler](TParserContext& ctx, TLocation loc) -> TAstTask {
            TParseHandleImpl handle{ctx};
            co_return co_await h(handle, loc);
        };
    }
    auto result = ParseExpr(context).result();
    if (!result) {
        return std::unexpected(result.error());
    }
    auto token = stream.Next();
    if (!IsEof(token)) {
        return std::unexpected(Error(token, "expected eof"));
    }
    return result;
}

} // namespace NCore
} // namespace NAst
} // namespace NQumir
