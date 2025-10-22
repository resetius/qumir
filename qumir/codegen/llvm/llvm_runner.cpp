#include "llvm_runner.h"
#include "llvm_codegen_impl.h"

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>

#include <sstream>
#include <iomanip>

namespace NQumir::NCodeGen {

using namespace NIR;

TLlvmRunner::TLlvmRunner()
{}

std::optional<std::string> TLlvmRunner::Run(std::unique_ptr<ILLVMModuleArtifacts> iartifacts, const std::string& entryPoint, std::string* error) {
    auto* artifacts = static_cast<TLLVMModuleArtifacts*>(iartifacts.get());
    if (!artifacts || !artifacts->Module) {
        if (error) *error = "null artifacts";
        return std::nullopt;
    }
    // Initialize targets once per process (idempotent in LLVM).
    static bool inited = false;
    if (!inited) {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();
        inited = true;
    }

    // Make symbols from the current process available to the JIT. On Linux,
    // this requires the executable to be linked with -rdynamic as well.
    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    // Build execution engine
    std::string eeErr;
    llvm::Module* rawModulePtr = artifacts->Module.get();
    llvm::EngineBuilder builder(std::move(artifacts->Module));
    builder.setEngineKind(llvm::EngineKind::JIT);
    auto ee = std::unique_ptr<llvm::ExecutionEngine>(builder.setErrorStr(&eeErr).create());
    if (!ee) {
        if (error) *error = std::string("ExecutionEngine create failed: ") + eeErr;
        return std::nullopt;
    }

    // Heuristic: last function in our internal Module is the newest __repl*; but
    // artifacts->Module may have different ordering. We search for name pattern.
    llvm::Module* mod = rawModulePtr;
    llvm::Function* target = nullptr;
    llvm::Function* last = nullptr;
    if (mod) {
        for (auto& f : *mod) {
            last = &f;
            std::string name = f.getName().str();
            if (name == entryPoint) target = &f; // keep last matching
        }
    }
    if (!target) target = last;
    if (!target) {
        if (error) *error = "no function in module";
        return std::nullopt;
    }

    // DEBUG: dump function IR
    //target->print(llvm::errs());
    //llvm::errs() << "\n";

    auto* ty = target->getFunctionType();
    if (ty->getNumParams() != 0) {
        // We only handle zero-arg functions currently.
        if (error) *error = "function requires arguments (unsupported)";
        return std::nullopt;
    }

    std::vector<llvm::GenericValue> noargs;
    auto gv = ee->runFunction(target, noargs);
    auto* retTy = ty->getReturnType();
    if (retTy->isVoidTy()) {
        return std::nullopt; // no value
    }
    std::ostringstream oss;
    if (retTy->isFloatTy()) {
        oss << std::fixed << std::setprecision(15) << gv.FloatVal;
    }
    if (retTy->isDoubleTy()) {
        oss << std::fixed << std::setprecision(15) << gv.DoubleVal;
    }
    if (retTy->isIntegerTy()) {
        unsigned bits = retTy->getIntegerBitWidth();
        if (bits == 1) {
            oss << (gv.IntVal.getZExtValue() ? "true" : "false");
        } else {
            oss << gv.IntVal.getSExtValue();
        }
    }
    return oss.str();
}

} // namespace NQumir::NCodeGen
