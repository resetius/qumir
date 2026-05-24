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
#include <functional>

#include <qumir/runtime/string.h> // for str_release
#include <qumir/runtime/runtime.h> // for __ensure and longjmp escape hatch
#include <qumir/runtime/robot.h>
#include <qumir/runtime/turtle.h>
#include <qumir/runtime/painter.h>
#include <qumir/runtime/future.h>

#include <cassert>


// Symbol anchors: prevent macOS from dead-stripping __qumir_future_* and
// __qumir_wrap_coro. These are only called from JIT-compiled IR, so the
// static linker sees no compile-time references; without this -rdynamic
// has nothing to export for the JIT symbol lookup. The anchor lives here
// (in llvm_runner.cpp) because this TU is always included in the link.
__attribute__((used))
static const void* const kQumir_jit_symbol_anchors[] = {
    reinterpret_cast<const void*>(&NQumir::__qumir_future_destroy),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_done),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_resume),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_address),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_await_ready),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_await_suspend),
    reinterpret_cast<const void*>(&NQumir::__qumir_future_await_resume),
    reinterpret_cast<const void*>(&NQumir::__qumir_wrap_coro),
};

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

std::optional<std::string> TLlvmRunner::Run(
    std::unique_ptr<ILLVMModuleArtifacts> iartifacts,
    const std::string& entryPoint,
    std::string* error,
    bool returnTypeIsString,
    std::function<std::optional<std::string>(const void*)> coroutineResultFormatter) {
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
    llvm::Function* coroPromisePtrFunc = nullptr;
    if (mod) {
        for (auto& f : *mod) {
            last = &f;
            std::string name = f.getName().str();
            if (name == entryPoint) target = &f; // keep last matching
            if (name == "$$module_constructor") constructorFunc = &f;
            if (name == "$$module_destructor") destructorFunc = &f;
            if (name == "__qumir_coro_promise_ptr") coroPromisePtrFunc = &f;
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

    const bool isCoroutineModule = (mod->getGlobalVariable("__qumir_is_coroutine") != nullptr);

    std::vector<llvm::GenericValue> noargs;
    if (constructorFunc) {
        SafeRunFunction(ee.get(), constructorFunc, noargs);
    }
    auto gv = SafeRunFunction(ee.get(), target, noargs);

    auto processEvents = [&]() {
        size_t processed = 0;
        processed += NRuntime::robot_process_events();
        processed += NRuntime::turtle_process_events();
        processed += NRuntime::painter_process_events();
        processed += NRuntime::io_process_events();
        return processed;
    };

    if (isCoroutineModule) {
        // Wrap the raw coro frame in ITypeErasedFuture* and use the public
        // __qumir_future_* API for the event loop. This avoids any dependency
        // on the __qumir_coro_* LLVM-intrinsic wrappers.
        ITypeErasedFuture* future = __qumir_wrap_coro(gv.PointerVal, 0);

        while (!__qumir_future_done(future)) {
            size_t processed = processEvents();
            assert(processed > 0 && "coroutine suspended with no pending async events");
            if (!__qumir_future_done(future)) {
                __qumir_future_resume(future);
            }
        }
        // Flush any remaining batched calls (e.g. painter drawing commands).
        processEvents();

        std::optional<std::string> result;
        if (coroutineResultFormatter && coroPromisePtrFunc) {
            using TPromisePtrFn = void* (*)(void*);
            auto addr = ee->getFunctionAddress(coroPromisePtrFunc->getName().str());
            if (addr == 0) {
                if (error) *error = "failed to resolve __qumir_coro_promise_ptr";
            } else {
                auto* promisePtrFn = reinterpret_cast<TPromisePtrFn>(addr);
                result = coroutineResultFormatter(promisePtrFn(gv.PointerVal));
            }
        }

        __qumir_future_destroy(future);

        if (destructorFunc) {
            SafeRunFunction(ee.get(), destructorFunc, noargs);
        }
        return result;
    }

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
