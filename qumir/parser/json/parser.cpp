#include "parser.h"

#include <algorithm>

namespace NQumir {
namespace NAst {
namespace NJson {

namespace {

struct TContext {
    TWrappedTokenStream& Stream;
    TJson Json;
};

TError Error(const TToken& token, const std::string& message) {
    return TError(token.Location, message);
}

TJsonTask value(TContext& ctx);
TJsonTask array(TContext& ctx);

TJsonTask array(TContext& ctx) {
    auto jarray = ctx.Json.NewNode<TArray>();
    auto token = ctx.Stream.Next();
    if (token.Type == TToken::Operator && token.Value.i64 == ']') {
        co_return jarray;
    }
    ctx.Stream.Unget(token);

    while (true) {
        auto jvalue = co_await value(ctx);
        jarray->Elements.push_back(jvalue);

        token = ctx.Stream.Next();
        if (token.Type == TToken::Operator && token.Value.i64 == ']') {
            co_return jarray;
        }

        if (!(token.Type == TToken::Operator && token.Value.i64 == ',')) {
            co_return Error(token, "expected ',' or ']'");
        }
    }

    co_return TError(ctx.Stream.GetLocation(), "unexpected end of input while parsing array");
}

TJsonTask object(TContext& ctx) {
    auto jobject = ctx.Json.NewNode<TObject>();

    auto finalize = [&]() {
        std::sort(jobject->Members.begin(), jobject->Members.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });
    };

    auto token = ctx.Stream.Next();
    if (token.Type == TToken::Operator && token.Value.i64 == '}') {
        finalize();
        co_return jobject;
    }

    ctx.Stream.Unget(token);

    while (true) {
        auto keyToken = ctx.Stream.Next();
        if (keyToken.Type != TToken::String) {
            co_return Error(keyToken, "expected string key in object");
        }
        std::string key = std::move(keyToken.Name);

        auto colonToken = ctx.Stream.Next();
        if (colonToken.Type != TToken::Operator || colonToken.Value.i64 != ':') {
            co_return Error(colonToken, "expected ':' after key in object");
        }

        auto valueNode = co_await value(ctx);
        jobject->Members.emplace_back(std::move(key), valueNode);

        token = ctx.Stream.Next();
        if (token.Type == TToken::Operator && token.Value.i64 == '}') {
            finalize();
            co_return jobject;
        }

        if (!(token.Type == TToken::Operator && token.Value.i64 == ',')) {
            co_return Error(token, "expected ',' or '}' in object");
        }
    }

    co_return TError(ctx.Stream.GetLocation(), "unexpected end of input while parsing object");
}

TJsonTask value(TContext& ctx) {
    auto token = ctx.Stream.Next();
    if (token.Type == TToken::Operator) {
        switch (token.Value.i64) {
            case '{':
                co_return co_await object(ctx);
            case '[':
                co_return co_await array(ctx);
            default:
                co_return Error(token, "unexpected operator");
        }
    } else if (token.Type == TToken::String) {
        co_return ctx.Json.NewNode<TString>(std::move(token.Name));
    } else if (token.Type == TToken::Integer) {
        co_return ctx.Json.NewNode<TInteger>(token.Value.i64);
    } else if (token.Type == TToken::Float) {
        co_return ctx.Json.NewNode<TFloat>(token.Value.f64);
    } else if (token.Type == TToken::Keyword) {
        if (token.Name == "true") {
            co_return ctx.Json.NewNode<TBoolean>(true);
        } else if (token.Name == "false") {
            co_return ctx.Json.NewNode<TBoolean>(false);
        } else if (token.Name == "null") {
            co_return ctx.Json.NewNode<TNull>();
        } else {
            co_return Error(token, "unexpected keyword");
        }
    } else {
        co_return Error(token, "unexpected token type");
    }
}

} // namespace

std::expected<TJson, TError> TParser::Parse(TTokenStream& baseStream) {
    TWrappedTokenStream stream(baseStream, 4);
    TContext ctx{stream};
    auto result = value(ctx).result();
    if (!result) {
        return std::unexpected(result.error());
    }

    try {
        auto token = stream.Next();
        if (!token.IsEof()) {
            return std::unexpected(Error(token, "unexpected token after JSON value"));
        }
    } catch (const TError& err) {
        // the lexer reports failures by throwing, unlike the coroutines above
        return std::unexpected(err);
    }

    return std::move(ctx.Json);
}

} // namespace NJson
} // namespace NAst
} // namespace NQumir