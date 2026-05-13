/*
Core language lexical and surface syntax notes.

Delimiters:
  ( )  expression/statement forms and nested item lists
  < >  composite type forms
  :    type annotation separator

Forms:
  (if cond then else)
      If expression.

  (cond cond then)
  (cond cond then else)
      Conditional statement, with optional else branch.

  (block stmt1 stmt2 ... stmtN)
      Block statement.

  (let
      (var1 value1)
      (var2 value2)
      ...
      body)
      Let expression. Bindings are visible in body only; bindings are not
      visible to each other.

  (: ast_node type)
      Type annotation.

Types:
  Primitive:
      i32
      f64
      bool

  Composite:
      <array element_type arity>
      <ptr pointee_type>
      <ref referenced_type>
      <struct (field_name1 field_type1) ... (field_nameN field_typeN)>
      <named name underlying_type>

  Composite type examples:
      <array i32 1>
      <array <ref f64> 2>
      <struct (x i32) (values <array f64 1>)>
      <named color i32>

Literals:
  Integer:
      123
      0
      42

  Float:
      1.23
      0.0
      .5
      3e-4

  Boolean:
      #t
      #f

  String:
      "hello"
      "world"

  Character:
      'a'
      '\n'

Identifiers:
  Simple identifiers start with a letter and may contain letters, digits,
  and underscores:
      foo
      bar
      my_var

  Bar identifiers may contain spaces:
      |foo bar|

Escapes:
  Strings and characters support common escapes:
      \n
      \t
      \\
      \"
      \'
*/

#include "lexer.h"

#include <qumir/parser/operator.h>

#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace NQumir {
namespace NAst {
namespace NCore {

namespace {

static constexpr auto Eof = std::istream::traits_type::eof();

static const std::unordered_set<int> Operators = {
    '(',
    ')',
    '<',
    '>',
    ':',
};

bool IsIdentStart(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalpha(uch) || ch == '_' || uch >= 0x80;
}

bool IsIdentContinue(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) || ch == '_' || uch >= 0x80;
}

} // namespace

TTokenStream::TTokenStream(std::istream& in)
    : ITokenStream(in)
{ }

void TTokenStream::Read() {
    auto take = [&]() {
        char ch = 0;
        In.get(ch);
        AdvanceLocation(CurrentLocation, ch);
        return ch;
    };

    auto emitOperator = [&](TOperator op, const std::string& rawValue, TLocation location) {
        Tokens.emplace_back(TToken {
            .Value = {.i64 = (int64_t)op},
            .RawValue = rawValue,
            .Type = TToken::Operator,
            .Location = location,
        });
    };

    auto emitIdentifier = [&](const std::string& name, TLocation location) {
        Tokens.emplace_back(TToken {
            .Name = name,
            .RawValue = name,
            .Type = TToken::Identifier,
            .Location = location,
        });
    };

    auto readQuoted = [&](char quote) {
        std::string value;
        std::string rawValue;
        bool escaped = false;

        while (In.peek() != Eof) {
            char ch = take();
            rawValue += ch;
            if (escaped) {
                value += Unescape(ch);
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                rawValue.pop_back();
                return std::make_pair(value, rawValue);
            } else {
                value += ch;
            }
        }

        throw std::runtime_error("unterminated literal");
    };

    auto readNumber = [&](TLocation location) {
        std::string rawValue;
        bool isFloat = false;

        if (In.peek() == '.') {
            isFloat = true;
            rawValue += take();
        }

        while (In.peek() != Eof && std::isdigit(In.peek())) {
            rawValue += take();
        }

        if (In.peek() == '.') {
            isFloat = true;
            rawValue += take();
            while (In.peek() != Eof && std::isdigit(In.peek())) {
                rawValue += take();
            }
        }

        if (In.peek() == 'e' || In.peek() == 'E') {
            isFloat = true;
            rawValue += take();
            if (In.peek() == '+' || In.peek() == '-') {
                rawValue += take();
            }
            if (In.peek() == Eof || !std::isdigit(In.peek())) {
                throw std::runtime_error("expected digit in exponent at " + CurrentLocation.ToString());
            }
            while (In.peek() != Eof && std::isdigit(In.peek())) {
                rawValue += take();
            }
        }

        if (isFloat) {
            Tokens.emplace_back(TToken {
                .Value = {.f64 = std::stod(rawValue)},
                .RawValue = rawValue,
                .Type = TToken::Float,
                .Location = location,
            });
        } else {
            Tokens.emplace_back(TToken {
                .Value = {.i64 = std::stoll(rawValue)},
                .RawValue = rawValue,
                .Type = TToken::Integer,
                .Location = location,
            });
        }
    };

    while (Tokens.empty() && In.peek() != Eof) {
        auto next = In.peek();
        if (std::isspace(next)) {
            take();
            continue;
        }

        TLocation tokenLocation = CurrentLocation;

        if (Operators.contains(next)) {
            emitOperator(TOperator((uint64_t)next), std::string(1, take()), tokenLocation);
        } else if (std::isdigit(next) || next == '.') {
            readNumber(tokenLocation);
        } else if (next == '#') {
            take();
            char value = take();
            if (value != 't' && value != 'f') {
                throw std::runtime_error("expected #t or #f at " + tokenLocation.ToString());
            }
            Tokens.emplace_back(TToken {
                .Value = {.i64 = value == 't' ? 1 : 0},
                .RawValue = std::string("#") + value,
                .Type = TToken::Boolean,
                .Location = tokenLocation,
            });
        } else if (next == '"') {
            take();
            auto [value, rawValue] = readQuoted('"');
            Tokens.emplace_back(TToken {
                .Name = value,
                .RawValue = rawValue,
                .Type = TToken::String,
                .Location = tokenLocation,
            });
        } else if (next == '\'') {
            take();
            auto [value, rawValue] = readQuoted('\'');
            auto chCode = AsSingleCharCode(value);
            if (!chCode) {
                throw std::runtime_error("character literal must contain exactly one character at " + tokenLocation.ToString());
            }
            Tokens.emplace_back(TToken {
                .Value = {.i64 = *chCode},
                .RawValue = rawValue,
                .Type = TToken::Char,
                .Location = tokenLocation,
            });
        } else if (next == '|') {
            take();
            std::string name;
            while (In.peek() != Eof && In.peek() != '|') {
                name += take();
            }
            if (In.peek() != '|') {
                throw std::runtime_error("unterminated bar identifier at " + tokenLocation.ToString());
            }
            take();
            emitIdentifier(name, tokenLocation);
        } else if (IsIdentStart(next)) {
            std::string name;
            do {
                name += take();
            } while (In.peek() != Eof && IsIdentContinue(static_cast<char>(In.peek())));
            emitIdentifier(name, tokenLocation);
        } else {
            throw std::runtime_error("unexpected character '" + std::string(1, next) + "' at " + tokenLocation.ToString());
        }
    }
}

} // namespace NCore
} // namespace NAst
} // namespace NQumir
