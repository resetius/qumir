#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void PromoteLocalsToSSA(TFunction& function, TModule& module);
void PromoteLocalsToSSA(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir