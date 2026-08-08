#include "lexer.h"

#include <unordered_set>

namespace NQumir {
namespace NAst {
namespace NJson {

namespace {

static constexpr auto Eof = std::istream::traits_type::eof();

static const std::unordered_set<int> Operators = {
    '{',
    '}',
    '[',
    ']',
    ':',
    ',',
};

static const std::unordered_set<std::string> Keywords = {
    "true",
    "false",
    "null",
};

} // namespace

TTokenStream::TTokenStream(std::istream& in)
    : ITokenStream(in)
{ }

// The base lexer reports failures with plain exceptions; give every one of them
// a location so the parser can hand out a TError instead.
void TTokenStream::Read() {
    try {
        ReadTokens();
    } catch (const TError&) {
        throw;
    } catch (const std::exception& exc) {
        throw TError(CurrentLocation, exc);
    }
}

void TTokenStream::ReadTokens() {
    while (Tokens.empty() && In.peek() != Eof) {
        auto next = In.peek();
        if (std::isspace(next)) {
            Take();
            continue;
        }
        TLocation tokenLocation = CurrentLocation;

        if (std::isdigit(next) || next == '.' || (next == '-' && [&]() {
            In.get();
            auto second = In.peek();
            In.unget(next);
            return std::isdigit(second) || second == '.';
        }())) {
            ReadNumber(tokenLocation);
        } else if (next == '"') {
            Take();
            auto [value, rawValue] = ReadQuoted('"');
            Tokens.emplace_back(TToken {
                .Name = value,
                .RawValue = rawValue,
                .Type = TToken::String,
                .Location = tokenLocation,
            });
        } else if (Operators.contains(next)) {
            Take();
            Tokens.emplace_back(TToken {
                .Value = {.i64 = next},
                .RawValue = std::string(1, next),
                .Type = TToken::Operator,
                .Location = tokenLocation,
            });
        } else if (std::isalpha(next)) {
            std::string name;
            do {
                name += Take();
            } while (In.peek() != Eof && std::isalnum(In.peek()));
            if (Keywords.contains(name)) {
                Tokens.emplace_back(TToken {
                    .Name = name,
                    .RawValue = name,
                    .Type = TToken::Keyword,
                    .Location = tokenLocation,
                });
            } else {
                throw TError(tokenLocation, "unexpected identifier '" + name + "'");
            }
        } else {
            throw TError(tokenLocation, "unexpected character '" + std::string(1, next) + "'");
        }
    }
}

} // namespace NJson
} // namespace NAst
} // namespace NQumir
