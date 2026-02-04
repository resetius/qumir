#pragma once

#include <qumir/modules/module.h>

namespace NQumir {
namespace NRegistry {

class DrawerModule : public IModule {
public:
    DrawerModule();

    const std::string& Name() const override {
        static const std::string name = "Чертежник";
        return name;
    }

    const std::vector<TExternalFunction>& ExternalFunctions() const override {
        return ExternalFunctions_;
    }

private:
    std::vector<TExternalFunction> ExternalFunctions_;
};

} // namespace NRegistry
} // namespace NQumir
