#pragma once

#include <qumir/location.h>
#include <qumir/parser/pragma.h>

#include <string>
#include <deque>
#include <optional>
#include <istream>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "lexer_base.h"

namespace NQumir {

namespace NAst {

// алг, нач, кон, если, то, иначе, все, нц, кц, кц_при,
// ввод, вывод, цел, вещ, лог, лит, таб, выбор, при
// для, от, до, шаг, раз, и, или, не, div, mod

enum class EKeyword : uint8_t {
    False,
    True,
    Alg,
    Begin,
    End,
    If,
    Then,
    Else,
    EndIf,
    Break,
    Continue,
    Switch,
    Case,
    LoopStart,
    LoopEnd,
    LoopEndWhen,
    Input,
    Output,

    Int,
    Float,
    Bool,
    String,
    Char,

    IntTab,
    FloatTab,
    BoolTab,
    StringTab,
    CharTab,

    Array,
    File,
    For,
    While,
    From,
    To,
    Step,
    Times,
    NewLine,
    InArg,
    OutArg,
    InOutArg,
    Return,
    Use,
    Assert,
    AssertBefore,
    AssertAfter,

    NamedType, // for built-in types imported from modules
};

enum class EOperator : uint8_t {
    // Arithmetic operators
    Pow = 1, // **
    Mul, // *
    FDiv, // /
    Plus, // +
    Minus, // -
    // Comparison operators
    Eq, // =
    Neq, // <>
    Lt, // <
    Gt, // >
    Leq, // <=
    Geq, // >=
    // Other operators
    Assign, // :=
    Comma, // ,
    LParen, // (
    RParen, // )
    LSqBr, // [
    RSqBr, // ]
    Colon, // :
    // Logical operators
    And,
    Or,
    Not,
    // Special operators
    Eol = (unsigned char)-2, // \n
    Eof = (unsigned char)-1, // end of file
};

class ILexerContext {
public:
    virtual ~ILexerContext() = default;
    virtual void ImportTypeNames(const std::vector<std::string>& names) = 0;
    virtual void ImportLiteralSuffix(const std::string& suffix, const std::string& ctorFn) = 0;
    virtual const std::unordered_map<std::string, std::string>& GetLiteralSuffixes() const = 0;
    virtual const std::vector<TPragma>& GetPragmas() const = 0;
};

struct TLexerContext : public ILexerContext {
    void ImportTypeNames(const std::vector<std::string>& names) override {
        for (const auto& name : names) {
            TypeNames.insert(name);
        }
    }

    void ImportLiteralSuffix(const std::string& suffix, const std::string& ctorFn) override {
        LiteralSuffixMap[suffix] = ctorFn;
    }

    const std::unordered_map<std::string, std::string>& GetLiteralSuffixes() const override {
        return LiteralSuffixMap;
    }

    const std::vector<TPragma>& GetPragmas() const override {
        return Pragmas;
    }

    std::unordered_set<std::string> TypeNames;
    std::unordered_map<std::string, std::string> LiteralSuffixMap;
    std::vector<TPragma> Pragmas;
};

class TTokenStream : public ITokenStream
{
public:
    TTokenStream(std::istream& in);
    ILexerContext* GetContext() { return &Context; }

private:
    void Read() override;

    TLexerContext Context;
};

} // namespace NAst
} // namespace NQumir
