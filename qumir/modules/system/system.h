#pragma once

#include <qumir/modules/module.h>

namespace NQumir {
namespace NRegistry {

class SystemModule : public IModule {
public:
    const std::string& GetName() const override {
        static const std::string name = "SystemModule";
        return name;
    }

    void Register(NSemantics::TNameResolver& ctx) override;
};

} // namespace NRegistry
} // namespace NQumir