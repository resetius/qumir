#pragma once

#include <qumir/semantics/name_resolution/name_resolver.h>

namespace NQumir {
namespace NRegistry {

struct TExternalFunction {
    std::string Name;
    std::string MangledName;
    void* Ptr = nullptr;
    using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);
    TPacked Packed = nullptr; // packed thunk
    std::vector<NAst::TTypePtr> ArgTypes;
    NAst::TTypePtr ReturnType;
    bool RequireArgsMaterialization = false; // if true, arguments must be materialized before calling, used for strings
    bool IsOp = false; // if true, Name is an operator symbol; no name conflict check on import

    mutable std::vector<uint32_t> NameCodePoints;
};

struct TExternalType {
    std::string Name;
    NAst::TTypePtr Type; // must be unwrapped type, not named type
    bool IsAlias = false;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual const std::string& Name() const = 0;
    virtual const std::vector<TExternalFunction>& ExternalFunctions() const = 0;
    virtual const std::vector<TExternalType>& ExternalTypes() const = 0;
};

} // namespace NRegistry
} // namespace NQumir