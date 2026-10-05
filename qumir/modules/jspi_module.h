#pragma once

#include <qumir/modules/module.h>

namespace NQumir {
namespace NRegistry {

// JSPI returns the result of an async import directly, without a Future handle.
class TJspiModule : public IModule {
public:
    explicit TJspiModule(std::shared_ptr<IModule> module)
        : Module_(std::move(module))
        , Functions_(Module_->ExternalFunctions())
    {
        for (auto& function : Functions_) {
            if (auto result = NAst::FutureResultType(function.ReturnType)) {
                AsyncImports_.push_back(function.MangledName);
                function.ReturnType = std::move(result);
            }
        }
    }

    const std::string& Name() const override {
        return Module_->Name();
    }
    const std::vector<TExternalFunction>& ExternalFunctions() const override {
        return Functions_;
    }
    const std::vector<TExternalType>& ExternalTypes() const override {
        return Module_->ExternalTypes();
    }
    const std::vector<TLiteralSuffix>& LiteralSuffixes() const override {
        return Module_->LiteralSuffixes();
    }
    const std::vector<std::string>& Dependencies() const override {
        return Module_->Dependencies();
    }
    bool IsImplicit() const override {
        return Module_->IsImplicit();
    }
    const std::vector<std::string>& AsyncImports() const {
        return AsyncImports_;
    }

private:
    std::shared_ptr<IModule> Module_;
    std::vector<TExternalFunction> Functions_;
    std::vector<std::string> AsyncImports_;
};

} // namespace NRegistry
} // namespace NQumir
