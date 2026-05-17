/*
Core language lexical and surface syntax notes.

Delimiters:
  ( )  expression/statement forms and nested item lists
  < >  composite type forms
  :    type annotation separator

AST node forms:
  Form names describe the core language surface syntax. They intentionally do
  not have to match NAst node NodeId values one-to-one.

  Literals:
      integer, float, boolean literal -> Number
      "string"                        -> StringLiteral
      'c'                             -> Number with Char type

  name // token with identifier type
      TIdentExpr, NodeId = "Ident".

  (= name value)
      TAssignExpr, NodeId = "Assign".

  (= name [index1 ... indexN] value)
      TArrayAssignExpr, NodeId = "ArrayAssign".

  (+ operand) // 1 operand after operator
      TUnaryExpr, NodeId = "Unary".

  (+ left right)
      TBinaryExpr, NodeId = "Binary".

  (block stmt1 stmt2 ... stmtN)
      TBlockExpr, NodeId = "Block", introduces a nested lexical scope.

  (seq stmt1 stmt2 ... stmtN)
      TSeqExpr, NodeId = "Seq", evaluates statements in order without
      introducing a nested lexical scope.

  (cond cond then else)
      TIfStmt, NodeId = "IfStmt".

  (if cond then else)
      TIfExpr, NodeId = "IfExpr".

  (let
      ((name1 value1) (name2 value2) ... (nameN valueN))
      body)
      TLetExpr, NodeId = "LetExpr". Bindings are visible in body only;
      bindings are not visible to each other.

  (while cond body)
      TWhileStmtExpr, NodeId = "WhileStmt".

  (repeat body cond)
      TRepeatStmtExpr, NodeId = "RepeatStmt".

  (for name from to step body)
      TForStmtExpr, NodeId = "ForStmt"; step is nil when omitted.

  (times count body)
      TTimesStmtExpr, NodeId = "TimesStmt".

  break // treat as a keyword
      TBreakStmt, NodeId = "Break".

  continue // treat as a keyword
      TContinueStmt, NodeId = "Continue".

  (var name type)
  (var name type [bound_from1 bound_to1] ... [bound_fromN bound_toN])
      TVarStmt, NodeId = "Var".

  (vars var1 var2 ... varN)
      TVarsBlockExpr, NodeId = "VarsBlock".

  (fun name return_type (param1 ... paramN) (attr1 ... attrM) body)
      TFunDecl, NodeId = "FunDecl".

  (call callee arg1 arg2 ... argN)
      TCallExpr, NodeId = "Call".

  (await operand)
      TAwaitExpr, NodeId = "Await".

  (input arg1 arg2 ... argN)
      TInputExpr, NodeId = "Input".

  (output (expr1 width1 precision1) ... (exprN widthN precisionN))
      TOutputExpr, NodeId = "Output".

  (cast operand type)
      TCastExpr, NodeId = "Cast".

  (index index collection)
      TIndexExpr, NodeId = "Index".

  (index [index1 index2 ... indexN] collection)
      TMultiIndexExpr, NodeId = "MultiIndex".

  (slice [start end] collection)
      TSliceExpr, NodeId = "Slice".

  (use module_name)
      TUseExpr, NodeId = "Use".

  (assert expr)
      TAssertStmt, NodeId = "Assert".

  (field field_name object)
      TFieldAccessExpr, NodeId = "FieldAccess".

  (struct ((name1 field1) (name2 field2) ... (nameN fieldN)))
      TStructConstructExpr, NodeId = "StructConstruct".

  (field_assign object field_name value)
      TFieldAssignExpr, NodeId = "FieldAssign".

  (: ast_node type)
      Type annotation for any AST node; sets TExpr::Type.

Types:
  Primitive:
      i64       -> TIntegerType, TypeId = "Int"
      f64       -> TFloatType, TypeId = "Float"
      bool      -> TBoolType, TypeId = "Bool"
      string    -> TStringType, TypeId = "String"
      char      -> TSymbolType, TypeId = "Char"
      file      -> TFileType, TypeId = "File"
      void      -> TVoidType, TypeId = "Void"

  Composite:
      <fun return_type (param_type1 ... param_typeN) (attr1 ... attrM) body>
          TFunctionType, TypeId = "Fun".

      <array element_type arity>
          TArrayType, TypeId = "Array".

      <future result_type>
          TFutureType, TypeId = "Future".

      <ptr pointee_type>
          TPointerType, TypeId = "Ptr".

      <ref referenced_type>
          TReferenceType, TypeId = "Ref".

      <named name underlying_type>
          TNamedType, TypeId = "Named".

      <struct (field_name1 field_type1) ... (field_nameN field_typeN)>
          TStructType, TypeId = "Struct".

  Type examples:
      i64
      f64
      bool
      string
      char
      file
      void
      <fun bool (i64 f64) () body>
      <array i64 1>
      <future void>
      <array <ref f64> 2>
      <ptr <struct (x f64) (y f64)>>
      <ref string>
      <named color i64>
      <struct (x i64) (values <array f64 1>)>

  The type syntax is schema-like: composite types are enclosed in < >, while
  nested lists inside a type, such as function parameter lists and struct
  fields, are enclosed in ( ).

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
    '[',
    ']',
};

bool IsSymbolicIdentChar(char ch) {
    switch (ch) {
        case '+':
        case '-':
        case '*':
        case '/':
        case '=':
        case '!':
        case '&':
        case '^':
            return true;
        default:
            return false;
    }
}

bool IsIdentStart(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalpha(uch) || ch == '_' || ch == '$' || uch >= 0x80;
}

bool IsIdentContinue(char ch) {
    const auto uch = static_cast<unsigned char>(ch);
    return std::isalnum(uch) || ch == '_' || ch == '$' || ch == ':' || uch >= 0x80;
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
        AfterOpenParen_ = (rawValue == "(");
        Tokens.emplace_back(TToken {
            .Value = {.i64 = (int64_t)op},
            .RawValue = rawValue,
            .Type = TToken::Operator,
            .Location = location,
        });
    };

    auto emitIdentifier = [&](const std::string& name, TLocation location) {
        AfterOpenParen_ = false;
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

        if (In.peek() == '-') {
            rawValue += take();
        }

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

        if ((next == '<' || next == '>') && AfterOpenParen_ && [&]() {
            In.get();
            auto second = In.peek();
            In.unget();
            return second == '=' || second == next;
        }()) {
            // Two-char operator (>>, >=, <<, <=) only valid as operator head after '('
            std::string name;
            name += take();
            name += take();
            emitIdentifier(name, tokenLocation);
        } else if (Operators.contains(next)) {
            auto ch = take();
            emitOperator(TOperator((uint64_t)ch), std::string(1, ch), tokenLocation);
        } else if (std::isdigit(next) || next == '.' || (next == '-' && [&]() {
            In.get();
            auto second = In.peek();
            In.unget();
            return std::isdigit(second) || second == '.';
        }())) {
            readNumber(tokenLocation);
        } else if (next == '|') {
            take();
            if (In.peek() == '|') {
                take();
                emitIdentifier("||", tokenLocation);
            } else if (In.peek() == Eof || std::isspace(In.peek()) || In.peek() == ')') {
                emitIdentifier("|", tokenLocation);
            } else {
                std::string name;
                while (In.peek() != Eof && In.peek() != '|') {
                    name += take();
                }
                if (In.peek() != '|') {
                    throw std::runtime_error("unterminated bar identifier at " + tokenLocation.ToString());
                }
                take();
                emitIdentifier(name, tokenLocation);
            }
        } else if (IsSymbolicIdentChar(next)) {
            std::string name;
            do {
                name += take();
            } while (In.peek() != Eof && IsSymbolicIdentChar(static_cast<char>(In.peek())));
            emitIdentifier(name, tokenLocation);
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
