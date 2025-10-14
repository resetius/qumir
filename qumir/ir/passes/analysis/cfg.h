#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void BuildCfg(TFunction& function);
void BuildCfg(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir