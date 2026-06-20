#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

// Implements the sealed-block algorithm from Braun et al., "Simple and
// Efficient Construction of Static Single Assignment Form":
// https://c9x.me/compile/bib/braun13cc.pdf
void PromoteLocalsToSSA(TFunction& function, TModule& module);
void PromoteLocalsToSSA(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir
