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
#include <setjmp.h>
#include <stdexcept>

#include <qumir/runtime/string.h> // for str_release
#include <qumir/runtime/runtime.h> // for __ensure and longjmp escape hatch

namespace NQumir::NCodeGen {

using namespace NIR;

#ifdef __APPLE__
// On macOS, MCJIT needs __dso_handle for global constructors/destructors
// registered via __cxa_atexit. Provide a dummy symbol for the JIT to resolve.
extern "C" {
    void* __dso_handle = (void*)&__dso_handle;
} // extern "C"
#endif

// Wraps a single runFunction call with a setjmp guard so that if the JIT
// program calls __ensure which triggers longjmp, we rethrow as a normal
// C++ exception (through host frames) rather than trying to unwind through
// JIT frames that lack DWARF unwind info (fatal on macOS).
//
// Must be noinline: inlining into Run() would put C++ objects with dtors
// between the setjmp and the potential longjmp.
[[gnu::noinline]] static llvm::GenericValue SafeRunFunction(
    llvm::ExecutionEngine* ee,
    llvm::Function* func,
    const std::vector<llvm::GenericValue>& args)
{
    jmp_buf jb;
    __set_jmp_target(&jb);
    if (setjmp(jb) != 0) {
        __clear_jmp_target();
        throw std::runtime_error(__get_runtime_error());
    }
    auto result = ee->runFunction(func, args);
    __clear_jmp_target();
    return result;
}

TLlvmRunner::TLlvmRunner()
{}

std::optional<std::string> TLlvmRunner::Run(std::unique_ptr<ILLVMModuleArtifacts> iartifacts, const std::string& entryPoint, std::string* error, bool returnTypeIsString) {
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
    llvm::Function* constructorFunc = nullptr;
    llvm::Function* destructorFunc = nullptr;
    if (mod) {
        for (auto& f : *mod) {
            last = &f;
            std::string name = f.getName().str();
            if (name == entryPoint) target = &f; // keep last matching
            if (name == "$$module_constructor") constructorFunc = &f;
            if (name == "$$module_destructor") destructorFunc = &f;
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
    if (constructorFunc) {
        SafeRunFunction(ee.get(), constructorFunc, noargs);
    }
    auto gv = SafeRunFunction(ee.get(), target, noargs);
    if (destructorFunc) {
        SafeRunFunction(ee.get(), destructorFunc, noargs);
    }
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
    if (retTy->isPointerTy()) {
        auto ptr = (char*)gv.PointerVal;
        if (ptr) {
            oss << ptr;
            // Release the string if returnTypeIsString
            if (returnTypeIsString) {
                NRuntime::str_release(ptr);
            }
        } else {
            oss << "(null)";
        }
    }

    return oss.str();
}

} // namespace NQumir::NCodeGen
