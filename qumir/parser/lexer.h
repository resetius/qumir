#pragma once

#include <qumir/location.h>

#include <string>
#include <deque>
#include <set>
#include <optional>
#include <istream>
#include <cstdint>

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
    All,
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
    Array,
    For,
    From,
    To,
    Step,
    Times,
    And,
    Or,
    Not,
    Div,
    Mod,
    Sqrt,
    Abs,
    Iabs,
    Sign,
    Sin,
    Cos,
    Tan,
    Ctg,
    Ln,
    Lg,
    Min,
    Max,
    Exp,
    IntFunc,
    Rnd,
};

enum class EOperator : uint8_t {
    // Arithmetic operators
    Pow, // **
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
    // Special operators
    Eol, // \n
};

struct TToken {
    enum EType {
        Integer,
        Float,
        String,
        Operator,
        Identifier,
        Keyword,
    };

    // everything except string fits in 8 bytes
    union UPrimitive {
        int64_t i64;
        double f64;
    };
    UPrimitive Value; // valid for Integer, Float, Operator, Keyword
    std::string Name; // valid for Identifier, String
    EType Type;
    TLocation Location;
};

class TTokenStream
{
public:
    TTokenStream(std::istream& in);
    std::optional<TToken> Next();
    void Unget(TToken token);
    const TLocation& operator()() const;
    const TLocation& GetLocation() const;

private:
    void Read();

    std::istream& In;
    std::deque<TToken> Tokens;
    TLocation CurrentLocation;
};

} // namespace NAst
} // namespace NQumir
