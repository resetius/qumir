#include "lexer_base.h"

namespace NQumir {
namespace NAst {

// Interpret a string literal value as a single character code if possible.
// Returns std::nullopt if the string does not represent exactly one character.
std::optional<int64_t> AsSingleCharCode(const std::string& s) {
    if (s.empty()) {
        return std::nullopt;
    }
    // Handle plain ASCII-single-byte case quickly
    if (static_cast<unsigned char>(s[0]) < 0x80) {
        if (s.size() != 1) {
            return std::nullopt;
        }
        return static_cast<int64_t>(static_cast<unsigned char>(s[0]));
    }

    // Validate exact one UTF-8 codepoint
    const unsigned char *p = reinterpret_cast<const unsigned char*>(s.data());
    size_t len = s.size();
    int64_t code = 0;
    int utf8bytesLeft = -1;

    for (size_t i = 0; i < len; ++i) {
        unsigned char c = p[i];
        if (utf8bytesLeft == -1) {
            if ((c & 0b10000000) == 0) {
                utf8bytesLeft = 0;
                code = c;
            } else if ((c & 0b11100000) == 0b11000000) {
                utf8bytesLeft = 1;
                code = c & 0b00011111;
            } else if ((c & 0b11110000) == 0b11100000) {
                utf8bytesLeft = 2;
                code = c & 0b00001111;
            } else if ((c & 0b11111000) == 0b11110000) {
                utf8bytesLeft = 3;
                code = c & 0b00000111;
            } else {
                return std::nullopt;
            }
            code <<= utf8bytesLeft * 6;
        } else {
            if ((c & 0b11000000) != 0b10000000) {
                return std::nullopt;
            }
            utf8bytesLeft--;
            code |= static_cast<int64_t>(c & 0b00111111) << (utf8bytesLeft * 6);
        }
    }

    if (utf8bytesLeft != 0) {
        // Not finished or overshot: not a single well-formed codepoint
        return std::nullopt;
    }
    return code;
}

char Unescape(char ch) {
    switch (ch) {
        case 'n': return '\n';
        case 't': return '\t';
        case '\\': return '\\';
        case '"': return '"';
        case '\'': return '\'';
        default:
            throw std::runtime_error("unknown escape sequence: \\" + std::string(1, ch));
    }
}

bool IsUtf8ContinuationByte(char ch) {
    return (static_cast<unsigned char>(ch) & 0b11000000) == 0b10000000;
}

void AdvanceLocation(TLocation& location, char ch) {
    if (ch == '\n') {
        location.Line++;
        location.Byte = 0;
        location.Column = 0;
        return;
    }

    if (!IsUtf8ContinuationByte(ch)) {
        location.Column++;
    }
    location.Byte++;
}

ITokenStream::ITokenStream(std::istream& in)
    : In(in)
    , CurrentLocation({1, 1, 1})
{ }

TToken ITokenStream::Next() {
    if (Tokens.empty()) {
        Read();
    }
    if (Tokens.empty()) {
        return TToken {
            .Value = {.i64 = -1},
            .Type = TToken::Operator,
            .Location = CurrentLocation
        };
    }
    TToken token = std::move(Tokens.front()); Tokens.pop_front();
    return token;
}

void ITokenStream::Unget(TToken token) {
    Tokens.emplace_front(std::move(token));
}

const TLocation& ITokenStream::operator()() const {
    return CurrentLocation;
}

const TLocation& ITokenStream::GetLocation() const {
    return CurrentLocation;
}

TWrappedTokenStream::TWrappedTokenStream(ITokenStream& baseStream, int windowSize)
    : BaseStream(baseStream)
    , WindowSize(windowSize)
{ }

TToken TWrappedTokenStream::Next() {
    auto tok = BaseStream.Next();
    Window.push_back(tok);
    while (static_cast<int>(Window.size()) > WindowSize) {
        Window.pop_front();
    }
    return tok;
}

void TWrappedTokenStream::Unget(TToken token) {
    BaseStream.Unget(token);
    Window.pop_back();
}

const TLocation& TWrappedTokenStream::operator()() const {
    return BaseStream();
}

const TLocation& TWrappedTokenStream::GetLocation() const {
    return BaseStream.GetLocation();
}

} // namespace NAst
} // namespace NQumir
