#pragma once

#include <qumir/error.h>
#include <qumir/location.h>
#include <qumir/parser/lexer_base.h>

#include <cstdint>
#include <deque>
#include <istream>
#include <string>

namespace NQumir {
namespace NAst {
namespace NJson {

class TTokenStream : public ITokenStream {
public:
    explicit TTokenStream(std::istream& in);

private:
    void Read() override;
    void ReadTokens();
};

} // namespace NJson
} // namespace NAst
} // namespace NQumir
