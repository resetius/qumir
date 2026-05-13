#pragma once

#include <qumir/location.h>

#include <string>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <vector>
#include <deque>
#include <istream>

namespace NQumir {
namespace NAst {

// Interpret a string literal value as a single character code if possible.
// Returns std::nullopt if the string does not represent exactly one character.
std::optional<int64_t> AsSingleCharCode(const std::string& s);
char Unescape(char ch);
bool IsUtf8ContinuationByte(char ch);
void AdvanceLocation(TLocation& location, char ch);

struct TToken {
    enum EType {
        Integer,
        Float,
        String,
        Char,
        Boolean,
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
    std::string Name; // valid for Identifier, String, NamedType
    std::string RawValue; // original raw value from source code (for error messages)
    EType Type;
    TLocation Location;
};

class ITokenStream {
public:
    explicit ITokenStream(std::istream& in);

    TToken Next();

    void Unget(TToken token);

    const TLocation& operator()() const;

    const TLocation& GetLocation() const;

protected:
    virtual void Read() = 0;

    std::istream& In;
    std::deque<TToken> Tokens;
    TLocation CurrentLocation;
};

class TWrappedTokenStream {
public:
    TWrappedTokenStream(ITokenStream& baseStream, int windowSize);

    TToken Next();
    void Unget(TToken token);
    const TLocation& operator()() const;
    const TLocation& GetLocation() const;
    const std::deque<TToken>& GetWindow() const { return Window; }

private:
    ITokenStream& BaseStream;
    std::deque<TToken> Window;
    int WindowSize;
};

} // namespace NAst
} // namespace NQumir
