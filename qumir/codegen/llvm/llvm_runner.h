#pragma once

#include "llvm_codegen.h"

#include <istream>
#include <string>
#include <optional>

namespace NQumir::NCodeGen {

// Runner: lowers code to NIR, translates to LLVM IR, returns full module IR text.
class TLlvmRunner {
public:
    TLlvmRunner();

    // No IR generation API here; only execution of already generated module.

    // Does not modify internal Module; purely consumes the artifacts.
    std::optional<std::string> Run(
        std::unique_ptr<ILLVMModuleArtifacts> artifacts,
        const std::string& entryPoint,
        std::string* error = nullptr);

private:
    std::string LastError; // currently unused (kept for future diagnostics)
};

} // namespace NQumir::NCodeGen
