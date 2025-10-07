#pragma once

#include <qumir/semantics/name_resolution/name_resolver.h>

namespace NQumir {
namespace NRegistry {

class IModule {
public:
    virtual ~IModule() = default;
    virtual const std::string& GetName() const = 0;
    virtual void Register(NSemantics::TNameResolver& ctx) = 0;
};

} // namespace NRegistry
} // namespace NQumir