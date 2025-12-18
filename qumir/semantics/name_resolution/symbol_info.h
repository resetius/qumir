#pragma once
#include <cstdint>

namespace NQumir {
namespace NSemantics {

struct TSymbolInfo {
    int32_t Id;
    int32_t DeclScopeId;
    int32_t ScopeLevelIdx;
    int32_t FunctionLevelIdx;
    int32_t FuncScopeId;
};

} // namespace NSemantics
} // namespace NQumir