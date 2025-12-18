#pragma once

#include <qumir/semantics/name_resolution/name_resolver.h>

namespace NQumir {
namespace NRegistry {

struct TExternalFunction {
    std::string Name;
    std::string MangledName;
    void* Ptr;
    using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);
    TPacked Packed = nullptr; // packed thunk
    std::vector<NAst::TTypePtr> ArgTypes;
    NAst::TTypePtr ReturnType;
    bool RequireArgsMaterialization = false; // if true, arguments must be materialized before calling, used for strings

    mutable std::vector<uint32_t> NameCodePoints;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual const std::string& Name() const = 0;
    virtual const std::vector<TExternalFunction>& ExternalFunctions() const = 0;
};

} // namespace NRegistry
} // namespace NQumir