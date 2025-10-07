#include "runner_ir.h"

#include <qumir/parser/lexer.h>
#include <qumir/modules/system/system.h>

#include <iostream>
#include <sstream>

namespace NQumir {

using namespace NIR;
using namespace NAst;

TIRRunner::TIRRunner(
    std::ostream& out,
    std::istream& in,
    TIRRunnerOptions options)
    : Builder(Module)
    , Lowerer(Module, Builder, Resolver)
    , Annotator(Resolver)
    , Options(std::move(options))
    , Interpreter(Module, Runtime, out, in)
{
    RegisteredModules.push_back(std::make_shared<NRegistry::SystemModule>());
    // TODO: register other modules

    for (const auto& mod : RegisteredModules) {
        mod->Register(Resolver);
    }
}

std::expected<std::optional<std::string>, TError> TIRRunner::Run(std::istream& input) {
    TTokenStream ts(input);
    TParser p;
    auto parsed = p.parse(ts);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    auto ast = std::move(parsed.value());
    auto scope = Resolver.GetOrCreateRootScope();
    scope->AllowsRedeclare = true; // TODO: move to options?
    scope->RootLevel = false;

    auto error = Resolver.Resolve(ast);
    if (error) {
        return std::unexpected(*error);
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

    auto lowerRes = Lowerer.LowerTop(ast);
    if (!lowerRes) {
        return std::unexpected(lowerRes.error());
    }
    auto* initFun = lowerRes.value();
    auto* mainFun = Module.GetEntryPoint();

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

    if (!mainFun) {
        return std::unexpected(TError(TLocation(), "no <main> function found"));
    }

    // Interpret
    auto res = Interpreter.Eval(*initFun, {});
    res = Interpreter.Eval(*mainFun, {});

    return res;
}

} // namespace NQumir
