#pragma once

#include <expected>
#include <string>
#include <vector>

#include <qumir/parser/type.h>

namespace NQumir {

namespace NRegistry {
    class IModule;

    struct TLiteralSuffix {
        std::string Suffix;
        std::string CtorFunction;
    };
} // namespace NRegistry

class IModuleManager {
public:
    virtual ~IModuleManager() = default;

    // Import a module by name.
    // Returns the module pointer on success, or a fully-formatted error message on failure
    // (unknown module or name conflict between modules).
    virtual std::expected<NRegistry::IModule*, std::string> ImportModule(const std::string& name) = 0;
    virtual NAst::TTypePtr LookupType(const std::string& name) const = 0;
    // All type names currently imported (including transitive dependencies)
    virtual std::vector<std::string> GetAllImportedTypeNames() const = 0;
    // All literal suffixes currently imported (including transitive dependencies)
    virtual std::vector<NRegistry::TLiteralSuffix> GetAllImportedLiteralSuffixes() const = 0;
};

} // namespace NQumir
