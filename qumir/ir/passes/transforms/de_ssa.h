#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void DeSSA(TFunction& function, TModule& module);
void DeSSA(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir