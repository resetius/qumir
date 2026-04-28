#pragma once

#include <expected>
#include <string>

namespace NQumir {

class IModuleManager {
public:
    virtual ~IModuleManager() = default;

    // Import a module by name. Returns void on success, or a fully-formatted
    // Russian error message on failure (unknown module or name conflict).
    virtual std::expected<void, std::string> ImportModule(const std::string& name) = 0;
};

} // namespace NQumir
