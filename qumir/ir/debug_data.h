#pragma once

#include <qumir/ir/builder.h>

namespace NQumir {
namespace NIR {

std::string SerializeRuntimeData(const TModule& module);
std::string SerializeDebugData(const TModule& module);

} // namespace NIR
} // namespace NQumir
