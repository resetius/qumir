#pragma once

#include <expected>

#include <qumir/error.h>
#include <qumir/optional.h>

#include "node.h"
#include "lexer.h"

namespace NQumir {
namespace NAst {
namespace NJson {

using TJsonTask = TExpectedTask<IValue*, TError, TLocation>;

class TParser {
public:
    std::expected<TJson, TError> Parse(TTokenStream& stream);
};

} // namespace NJson
} // namespace NAst
} // namespace NQumir
