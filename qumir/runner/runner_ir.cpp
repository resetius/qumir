#include "runner_ir.h"

#include <exception>
#include <qumir/parser/lexer.h>
#include <qumir/semantics/kumir/pipeline.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/parser/core/printer.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/modules/turtle/turtle.h>
#include <qumir/modules/robot/robot.h>
#include <qumir/modules/drawer/drawer.h>
#include <qumir/modules/painter/painter.h>
#include <qumir/modules/complex/complex.h>
#include <qumir/modules/colors/colors.h>
#include <qumir/modules/keyboard/keyboard.h>
#include <qumir/ir/passes/transforms/pipeline.h>
#include <qumir/frontend/source_module_loader.h>
#include <qumir/frontend/compose.h>

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
    AvailableModules.push_back(std::make_shared<NRegistry::DrawerModule>());
    AvailableModules.push_back(std::make_shared<NRegistry::PainterModule>());
    AvailableModules.push_back(std::make_shared<NRegistry::ComplexModule>());
    AvailableModules.push_back(std::make_shared<NRegistry::ColorsModule>());
    AvailableModules.push_back(std::make_shared<NRegistry::KeyboardModule>());

    for (const auto& mod : RegisteredModules) {
        Resolver.RegisterModule(mod.get());
    }
    for (const auto& mod : AvailableModules) {
        Resolver.RegisterModule(mod.get());
    }

    if (!Options.CoreInput) {
        // Kumir prelude: standard runtime modules plus legacy module aliases.
        for (const auto& mod : RegisteredModules) {
            (void)Resolver.ImportModule(mod->Name());
        }
        for (const auto& [alias, canonical] : NSemantics::NKumir::ModuleAliases()) {
            Resolver.RegisterModuleAlias(alias, canonical);
        }
    } else {
        // Core has no implicit prelude; the host imports what it needs.
        for (const auto& name : Options.Prelude) {
            (void)Resolver.ImportModule(name);
        }
    }
}

std::optional<TError> TIRRunner::LoadAndComposeModules(
    TExprPtr& ast, const std::vector<NAst::TPragma>& corePragmas)
{
    NFrontend::TSourceModuleLoader loader;
    for (const auto& dir : Options.ModuleSearchPaths) {
        loader.AddSearchPath(dir);
    }

    if (auto block = TMaybeNode<TBlockExpr>(ast)) {
        for (const auto& stmt : block.Cast()->Stmts) {
            auto use = TMaybeNode<TUseExpr>(stmt);
            if (use && loader.Resolvable(use.Cast()->ModuleName)) {
                if (auto loaded = loader.Load(use.Cast()->ModuleName); !loaded) {
                    return loaded.error();
                }
            }
        }
    }

    auto modules = loader.TopologicalOrder();
    if (modules.empty()) {
        Resolver.ApplyPragmas(corePragmas);
        return std::nullopt;
    }

    auto composed = NFrontend::Compose(modules, ast, corePragmas, "<main>");
    if (!composed) {
        return composed.error();
    }
    ast = std::move(composed->Ast);
    Resolver.ApplyPragmas(composed->Pragmas);
    return std::nullopt;
}

std::expected<std::optional<std::string>, TError> TIRRunner::Run(std::istream& input) {
    std::expected<TExprPtr, TError> parsed;
    std::vector<NAst::TPragma> corePragmas;
    if (Options.CoreInput) {
        NCore::TTokenStream ts(input);
        NCore::TParser p;
        parsed = p.Parse(ts);
        if (parsed) {
            corePragmas = std::move(p.Pragmas);
        }
    } else {
        TTokenStream ts(input);
        TParser p;
        parsed = p.parse(ts, &Resolver);
        if (parsed) {
            Resolver.ApplyPragmas(ts.GetContext()->GetPragmas());
        }
    }
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    auto ast = std::move(parsed.value());

    if (Options.CoreInput) {
        if (auto err = LoadAndComposeModules(ast, corePragmas)) {
            return std::unexpected(*err);
        }
    }

    auto scope = Resolver.GetOrCreateRootScope();
    // scope->AllowsRedeclare = true; // TODO: move to options?
    scope->RootLevel = false;

    if (Options.CoreInput && Options.ResolveCoreInput) {
        if (auto err = Resolver.Resolve(ast)) {
            return std::unexpected(*err);
        }
    }

    if (Options.PrintAst) {
        std::cerr << "=========== AST: ===========\n";
        std::cerr << ast << std::endl;
        std::cerr << "============================\n\n";
    }

    NTransform::TPipelineExtensions extensions;
    if (Options.CoreInput) {
        extensions.AfterTypeAnnotation.push_back(
            NTransform::CoroutineAnnotationTransform);
    } else {
        extensions = NSemantics::NKumir::PipelineExtensions();
    }
    auto pipelineOptions = NTransform::TPipelineOptions{
        .Extensions = std::move(extensions),
    };
    auto error = NTransform::Pipeline(ast, Resolver, std::move(pipelineOptions));
    if (!error) {
        return std::unexpected(error.error());
    }

    if (Options.PrintTransformedAst) {
        std::cerr << "===== TRANSFORMED AST: =====\n";
        std::cerr << ast << std::endl;
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
    try {
        auto res = Interpreter.Eval(*mainFun, {}, TInterpreter::TOptions{.PrintByteCode = Options.PrintByteCode});
        return res;
    } catch (const std::exception& e) {
        // TODO: free resources?
        return std::unexpected(TError(std::string("runtime error: ") + e.what()));
    }
}

} // namespace NQumir
