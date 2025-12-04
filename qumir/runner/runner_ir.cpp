#include "runner_ir.h"

#include <qumir/parser/lexer.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/modules/turtle/turtle.h>
#include <qumir/modules/robot/robot.h>
#include <qumir/ir/passes/transforms/pipeline.h>

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
    , Options(std::move(options))
    , Interpreter(Module, out, in)
{
    RegisteredModules.push_back(std::make_shared<NRegistry::SystemModule>());
    // TODO: register other modules

    AvailableModules.push_back(std::make_shared<NRegistry::TurtleModule>());
    AvailableModules.push_back(std::make_shared<NRegistry::RobotModule>());

    for (const auto& mod : RegisteredModules) {
        mod->Register(Resolver);
    }
    for (const auto& mod : AvailableModules) {
        Resolver.RegisterModule(mod.get());
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

    auto error = NTransform::Pipeline(ast, Resolver);
    if (!error) {
        return std::unexpected(error.error());
    }

    if (Options.PrintAst) {
        std::cerr << "=========== AST: ===========\n";
        std::cerr << *ast << std::endl;
        std::cerr << "============================\n\n";
    }

    auto lowerRes = Lowerer.LowerTop(ast);
    if (!lowerRes) {
        return std::unexpected(lowerRes.error());
    }
    if (Options.OptLevel > 0) {
        NIR::NPasses::Pipeline(Module);
    }

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
    auto res = Interpreter.Eval(*mainFun, {}, TInterpreter::TOptions{.PrintByteCode = Options.PrintByteCode});

    return res;
}

} // namespace NQumir
