#pragma once

#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/modules/module.h>

#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>

#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_runner.h>

#include <expected>
#include <istream>
#include <optional>
#include <unordered_set>

namespace NQumir {

struct TLLVMRunnerOptions {
    bool PrintAst = false;
    bool PrintTransformedAst = false;
    bool PrintIr = false;
    bool PrintLlvm = false;
    bool PrintAsm = false;
    bool NativeCode = false;
    bool CoreInput = false;
    bool ResolveCoreInput = true;
    bool AllowOverloads = false; // enable function overloads / generics (pragma language overloads)
    int OptLevel = 0; // 0-3
    // Core frontend host prelude: modules imported on behalf of the host.
    // Empty means pure core-lang imports nothing. Ignored for the Kumir
    // frontend, which imports its own prelude.
    std::vector<std::string> Prelude;
    // Directories searched for `.oz` source modules referenced by `use`.
    std::vector<std::string> ModuleSearchPaths;
};

class TLLVMRunner {
public:
    TLLVMRunner(TLLVMRunnerOptions options = {});

    void RegisterModule(std::shared_ptr<NRegistry::IModule> module, bool import = false);

    // Parses, resolves, lowers to IR and executes via LLVM JIT.
    // Returns numeric result (if any) produced by the compiled chunk.
    std::expected<std::optional<std::string>, TError> Run(std::istream& input);

    // Compiles a core-lang kernel source string and returns a JIT function pointer.
    // The pointer is valid for the lifetime of this TLLVMRunner.
    void* CompileKernel(const std::string& source, std::string* error = nullptr);
    // Selects the JIT entry point by source function name. This keeps the entry
    // stable when generic specializations are appended during type annotation.
    void* CompileKernelAst(
        NAst::TExprPtr ast, const std::string& entryName, std::string* error);

private:
    TLLVMRunnerOptions Options;
    // Persistent compiler state across Run() calls (REPL-style session)
    NIR::TModule Module;
    NIR::TBuilder Builder;
    NIR::TAstLowerer Lowerer;

    NSemantics::TNameResolver Resolver;

    std::unordered_set<int> PrintedChunks;
    std::unordered_set<int> PrintedLLVMChunks;
    std::unordered_set<std::string> RegisteredModuleNames;

    std::vector<std::shared_ptr<NRegistry::IModule>> RegisteredModules;
    std::vector<std::shared_ptr<NRegistry::IModule>> AvailableModules;

    NCodeGen::TLlvmRunner LlvmRunner_; // persistent; keeps compiled kernels alive
};

} // namespace NQumir
