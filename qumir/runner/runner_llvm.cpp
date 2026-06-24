#include "runner_llvm.h"

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

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>

namespace NQumir {

using namespace NIR;

TLLVMRunner::TLLVMRunner(TLLVMRunnerOptions options)
    : Options(std::move(options))
    , Builder(Module)
    , Lowerer(Module, Builder, Resolver)
{
    if (Options.AllowOverloads) {
        Resolver.ApplyPragmas({NAst::TPragma{"language", {"overloads"}, {}}});
    }

    RegisterModule(std::make_shared<NRegistry::SystemModule>(), true);
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
        (void)Resolver.ImportModule(mod->Name());
    }
    for (const auto& mod : AvailableModules) {
        Resolver.RegisterModule(mod.get());
    }

    if (!Options.CoreInput) {
        for (const auto& [alias, canonical] : NSemantics::NKumir::ModuleAliases()) {
            Resolver.RegisterModuleAlias(alias, canonical);
        }
    }
}

void TLLVMRunner::RegisterModule(std::shared_ptr<NRegistry::IModule> module, bool import) {
    if (!module) {
        return;
    }
    auto name = module->Name();
    if (RegisteredModuleNames.count(name)) {
        if (import) {
            (void)Resolver.ImportModule(name);
        }
        return;
    }
    Resolver.RegisterModule(module.get());
    RegisteredModuleNames.insert(name);
    RegisteredModules.push_back(std::move(module));
    if (import) {
        (void)Resolver.ImportModule(name);
    }
}

std::expected<std::optional<std::string>, TError> TLLVMRunner::Run(std::istream& input) {
    // Parse source into AST
    std::expected<NAst::TExprPtr, TError> parsed;
    if (Options.CoreInput) {
        NAst::NCore::TTokenStream ts(input);
        NAst::NCore::TParser p;
        parsed = p.Parse(ts);
        if (parsed) {
            Resolver.ApplyPragmas(p.Pragmas);
        }
    } else {
        NAst::TTokenStream ts(input);
        NAst::TParser p;
        parsed = p.parse(ts, &Resolver);
    }
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    auto ast = std::move(parsed.value());

    // Name resolution
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

    // Lower to IR: we build a fresh __repl function chunk per call
    auto lowerRes = Lowerer.LowerTop(ast);
    if (!lowerRes) {
        return std::unexpected(lowerRes.error());
    }

    if (Options.OptLevel > 0) {
        NIR::NPasses::Pipeline(Module);
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

    NCodeGen::TLLVMCodeGen cg({ .NativeCode = Options.NativeCode });
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

    if (Options.PrintAsm) {
        std::cerr << "=========== ASM: ===========\n";
        artifacts->Generate(std::cerr, /*generateAsm=*/true, /*generateObj=*/false);
        std::cerr << "============================\n\n";
    }

    auto* mainFun = Module.GetEntryPoint();
    if (!mainFun) {
        return std::unexpected(TError({}, std::string("entry point not found")));
    }

    // Run via LLVM JIT
    NCodeGen::TLlvmRunner runner;
    try {
        std::string runErr;
        auto coroutineResultFormatter = [&]() -> std::function<std::optional<std::string>(const void*)> {
            if (!mainFun->IsCoroutine || mainFun->CoroutineResultTypeId < 0
                || Module.Types.IsVoid(mainFun->CoroutineResultTypeId)) {
                return {};
            }
            return [&](const void* promisePtr) -> std::optional<std::string> {
                if (!promisePtr) {
                    return std::nullopt;
                }
                uint64_t bitRepr = 0;
                std::memcpy(&bitRepr, promisePtr,
                    std::min<int>(sizeof(bitRepr), Module.Types.SizeInBytes(mainFun->CoroutineResultTypeId)));
                std::ostringstream out;
                Module.Types.Format(out, bitRepr, mainFun->CoroutineResultTypeId);
                return out.str();
            };
        }();
        auto res = runner.Run(
            std::move(artifacts),
            mainFun->Name,
            &runErr,
            mainFun->ReturnTypeIsString,
            std::move(coroutineResultFormatter));
        if (!runErr.empty()) {
            return std::unexpected(TError({}, std::string("llvm run error: ") + runErr));
        }
        return res;
    } catch (const std::exception& e) {
        // TODO: free resources?
        return std::unexpected(TError(std::string("runtime error: ") + e.what()));
    }
}

void* TLLVMRunner::CompileKernelAst(
    NAst::TExprPtr ast, const std::string& entryName, std::string* error)
{
    auto scope = Resolver.GetOrCreateRootScope();
    // scope->AllowsRedeclare = true;
    scope->RootLevel = false;

    if (auto err = Resolver.Resolve(ast)) {
        if (error) {
            *error = err->ToString();
        }
        return nullptr;
    }

    auto transformResult = NTransform::Pipeline(ast, Resolver, {});
    if (!transformResult) {
        if (error) {
            *error = transformResult.error().ToString();
        }
        return nullptr;
    }

    auto lowerRes = Lowerer.LowerTop(ast);
    if (!lowerRes) {
        if (error) {
            *error = lowerRes.error().ToString();
        }
        return nullptr;
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

    if (Module.Functions.empty()) {
        if (error) {
            *error = "no functions after lowering";
        }
        return nullptr;
    }
    auto entry = std::find_if(
        Module.Functions.begin(), Module.Functions.end(),
        [&](const auto& function) { return function.Name == entryName; });
    if (entry == Module.Functions.end()) {
        if (error) {
            *error = "entry function not found: " + entryName;
        }
        return nullptr;
    }
    const std::string& funcName = entry->Name;

    NCodeGen::TLLVMCodeGen cg({ .NativeCode = Options.NativeCode });
    std::unique_ptr<NCodeGen::ILLVMModuleArtifacts> artifacts;
    try {
        artifacts = cg.Emit(Module, Options.OptLevel);
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("llvm codegen error: ") + e.what();
        }
        return nullptr;
    }

    if (Options.PrintLlvm) {
        artifacts->PrintModule(std::cerr);
    }
    if (Options.PrintAsm) {
        std::cerr << "=========== ASM: ===========\n";
        artifacts->Generate(std::cerr, /*generateAsm=*/true, /*generateObj=*/false);
        std::cerr << "============================\n\n";
    }

    std::string runErr;
    void* fnPtr = LlvmRunner_.Lookup(std::move(artifacts), funcName, &runErr);
    if (!fnPtr) {
        if (error) {
            *error = runErr.empty() ? "function not found: " + funcName : runErr;
        }
        return nullptr;
    }
    return fnPtr;
}

void* TLLVMRunner::CompileKernel(const std::string& source, std::string* error) {
    std::istringstream input(source);

    NAst::NCore::TTokenStream ts(input);
    NAst::NCore::TParser p;
    auto parsed = p.Parse(ts);
    if (!parsed) {
        if (error) {
            *error = parsed.error().ToString();
        }
        return nullptr;
    }

    auto scope = Resolver.GetOrCreateRootScope();
    // scope->AllowsRedeclare = true;
    scope->RootLevel = false;

    if (auto err = Resolver.Resolve(*parsed)) {
        if (error) {
            *error = err->ToString();
        }
        return nullptr;
    }

    auto transformResult = NTransform::Pipeline(*parsed, Resolver, {});
    if (!transformResult) {
        if (error) {
            *error = transformResult.error().ToString();
        }
        return nullptr;
    }

    auto lowerRes = Lowerer.LowerTop(*parsed);
    if (!lowerRes) {
        if (error) {
            *error = lowerRes.error().ToString();
        }
        return nullptr;
    }

    if (Module.Functions.empty()) {
        if (error) {
            *error = "no functions after lowering";
        }
        return nullptr;
    }
    const std::string funcName = Module.Functions.back().Name;

    NCodeGen::TLLVMCodeGen cg({ .NativeCode = Options.NativeCode });
    std::unique_ptr<NCodeGen::ILLVMModuleArtifacts> artifacts;
    try {
        artifacts = cg.Emit(Module, 0);
    } catch (const std::exception& e) {
        if (error) {
            *error = std::string("llvm codegen error: ") + e.what();
        }
        return nullptr;
    }

    std::string runErr;
    void* fnPtr = LlvmRunner_.Lookup(std::move(artifacts), funcName, &runErr);
    if (!fnPtr) {
        if (error) {
            *error = runErr.empty() ? "function not found: " + funcName : runErr;
        }
        return nullptr;
    }
    return fnPtr;
}

} // namespace NQumir
