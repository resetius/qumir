#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void ConstFold(TFunction& function, TModule& module);
void ConstFold(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir
