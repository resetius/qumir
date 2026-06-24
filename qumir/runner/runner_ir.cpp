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
        // Kumir frontend prelude: standard runtime modules + legacy aliases.
        for (const auto& mod : RegisteredModules) {
            (void)Resolver.ImportModule(mod->Name());
        }
        for (const auto& [alias, canonical] : NSemantics::NKumir::ModuleAliases()) {
            Resolver.RegisterModuleAlias(alias, canonical);
        }
    } else {
        // Core frontend: import only the host-provided prelude. Pure core-lang
        // imports nothing.
        for (const auto& name : Options.Prelude) {
            (void)Resolver.ImportModule(name);
        }
    }
}

std::expected<std::optional<std::string>, TError> TIRRunner::Run(std::istream& input) {
    std::expected<TExprPtr, TError> parsed;
    if (Options.CoreInput) {
        NCore::TTokenStream ts(input);
        NCore::TParser p;
        parsed = p.Parse(ts);
        if (parsed) {
            Resolver.ApplyPragmas(p.Pragmas);
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
