#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void Pipeline(TFunction& function, TModule& module);
void Pipeline(TModule& module);

void BeforeCompile(TFunction& function, TModule& module);
void BeforeCompile(TModule& module);

} // namespace NPasses
} // namespace NIR
} // namespace NQumir