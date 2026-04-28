#pragma once

#include <expected>
#include <string>

namespace NQumir {

namespace NRegistry {
    class IModule;
} // namespace NRegistry

class IModuleManager {
public:
    virtual ~IModuleManager() = default;

    // Import a module by name.
    // Returns the module pointer on success, or a fully-formatted error message on failure
    // (unknown module or name conflict between modules).
    virtual std::expected<NRegistry::IModule*, std::string> ImportModule(const std::string& name) = 0;
};

} // namespace NQumir
