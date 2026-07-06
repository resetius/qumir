#pragma once

#include "llvm_codegen.h"

#include <istream>
#include <string>
#include <optional>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace NQumir::NCodeGen {

struct TLlvmRunnerOptions {
    bool EnablePerfJitEventListener = false;
};

// Runner: lowers code to NIR, translates to LLVM IR, returns full module IR text.
class TLlvmRunner {
public:
    TLlvmRunner(TLlvmRunnerOptions options = {});

    // No IR generation API here; only execution of already generated module.

    // Does not modify internal Module; purely consumes the artifacts.
    std::optional<std::string> Run(
        std::unique_ptr<ILLVMModuleArtifacts> artifacts,
        const std::string& entryPoint,
        std::string* error = nullptr,
        bool returnTypeIsString = false /* TODO: remove me, clutch: support string returnType */,
        std::function<std::optional<std::string>(const void*)> coroutineResultFormatter = {});

    // Compiles the module via JIT and returns a function pointer by name.
    // The pointer is valid for the lifetime of this TLlvmRunner.
    void* Lookup(
        std::unique_ptr<ILLVMModuleArtifacts> artifacts,
        const std::string& name,
        std::string* error = nullptr);

    std::unordered_map<std::string, void*> LookupMany(
        std::unique_ptr<ILLVMModuleArtifacts> artifacts,
        const std::vector<std::string>& names,
        std::string* error = nullptr);

private:
    TLlvmRunnerOptions Options_;
    std::string LastError; // currently unused (kept for future diagnostics)

    // Keeps ExecutionEngines alive so function pointers returned by Lookup remain valid.
    // Type-erased to avoid including heavy LLVM headers here.
    std::vector<std::shared_ptr<void>> LiveEngines_;
};

} // namespace NQumir::NCodeGen
