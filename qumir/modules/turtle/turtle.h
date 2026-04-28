#pragma once

#include <qumir/modules/module.h>

namespace NQumir {
namespace NRegistry {

class TurtleModule : public IModule {
public:
    TurtleModule();

    const std::string& Name() const override {
        static const std::string name = "Черепаха";
        return name;
    }

    const std::vector<TExternalFunction>& ExternalFunctions() const override {
        return ExternalFunctions_;
    }

    const std::vector<TExternalType>& ExternalTypes() const override {
        return ExternalTypes_;
    }

private:
    std::vector<TExternalFunction> ExternalFunctions_;
    std::vector<TExternalType> ExternalTypes_;
};

} // namespace NRegistry
} // namespace NQumir