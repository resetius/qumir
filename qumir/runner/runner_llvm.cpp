#include "runner_llvm.h"

#include <qumir/parser/lexer.h>

#include <iostream>
#include <sstream>

namespace NQumir {

using namespace NIR;

std::expected<std::optional<std::string>, TError> TLLVMRunner::Run(std::istream& input) {
    // Parse source into AST
    NAst::TTokenStream ts(input);
    NAst::TParser p;
    auto parsed = p.parse(ts);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    auto ast = std::move(parsed.value());

    // Name resolution
    auto scope = Resolver.GetOrCreateRootScope();
    scope->AllowsRedeclare = true; // TODO: move to options?
    scope->RootLevel = false;
    auto error = Resolver.Resolve(ast);
    if (error.has_value()) {
        return std::unexpected(error.value());
    }
    auto annotationResult = Annotator.Annotate(ast);
    if (!annotationResult) {
        return std::unexpected(annotationResult.error());
    }
    ast = annotationResult.value();
    if (Options.PrintAst) {
        std::cerr << "=========== AST: ===========\n";
        std::cerr << *ast << std::endl;
        std::cerr << "============================\n\n";
    }

    // Lower to IR: we build a fresh __repl function chunk per call
    auto lowerRes = Lowerer.LowerTop(ast);
    if (!lowerRes) {
        return std::unexpected(lowerRes.error());
    }

    if (Options.PrintIr) {
        std::cerr << "=========== IR: ============\n";
        for (const auto& function : Module.Functions) {
            if (PrintedChunks.insert(function.UniqueId).second) {
                std::ostringstream oss;
                function.Print(oss, Module);
                std::cerr << oss.str() << std::endl;
            }
        }
        std::cerr << "============================\n\n";
    }

    // Emit LLVM IR for the module
    NCodeGen::TLLVMCodeGen cg({});
    std::string err;
    std::unique_ptr<NCodeGen::ILLVMModuleArtifacts> artifacts;
    try {
        artifacts = cg.Emit(Module, Options.OptLevel);
    } catch (const std::exception& e) {
        return std::unexpected(TError({}, std::string("llvm codegen error: ") + e.what()));
    }

    if (Options.PrintLlvm) {
        std::cerr << "=========== LLVM: ==========\n";
        for (const auto& function : Module.Functions) {
            if (PrintedLLVMChunks.insert(function.UniqueId).second) {
                cg.PrintFunction(function.SymId, std::cerr);
            }
        }
        std::cerr << "============================\n\n";
    }

    // Run via LLVM JIT
    NCodeGen::TLlvmRunner runner;
    std::string runErr;
    auto res = runner.Run(std::move(artifacts), &runErr);
    if (!runErr.empty()) {
        return std::unexpected(TError({}, std::string("llvm run error: ") + runErr));
    }
    return res;
}

} // namespace NQumir
