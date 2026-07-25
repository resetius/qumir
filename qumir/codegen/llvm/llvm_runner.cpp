#include "llvm_runner.h"
#include "llvm_codegen_impl.h"

#include <llvm/ADT/SmallString.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/JITEventListener.h>
#include <llvm/ExecutionEngine/GenericValue.h>
#include <llvm/ExecutionEngine/ObjectCache.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/SHA256.h>
#include <llvm/TargetParser/Host.h>

#include <cstdlib>
#include <sstream>
#include <iomanip>
#include <setjmp.h>
#include <stdexcept>
#include <functional>
#include <unordered_map>
#include <utility>

#include <qumir/runtime/string.h> // for str_release
#include <qumir/runtime/runtime.h> // for __ensure and longjmp escape hatch
#include <qumir/runtime/robot.h>
#include <qumir/runtime/turtle.h>
#include <qumir/runtime/drawer.h>
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

namespace {

std::pair<std::string, std::string> GetNativeCpuAndFeatures() {
    auto cpu = llvm::sys::getHostCPUName().str();
    std::string features;
    auto hostFeatures = llvm::sys::getHostCPUFeatures();
    for (const auto& feature : hostFeatures) {
        if (!features.empty()) {
            features += ",";
        }
        features += feature.getValue() ? "+" : "-";
        features += feature.getKey().str();
    }
    return {std::move(cpu), std::move(features)};
}

std::vector<std::string> SplitFeatures(const std::string& features) {
    std::vector<std::string> out;
    std::string current;
    for (char c : features) {
        if (c == ',') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        out.push_back(current);
    }
    return out;
}

/*
perf record -k 1 -g --call-graph dwarf ./your_qdb_command
perf inject --jit -i perf.data -o perf.jit.data
perf report -i perf.jit.data
*/
llvm::JITEventListener* CreatePerfJitEventListener() {
#if defined(__linux__) && defined(QUMIR_HAS_LLVM_PERF_JIT_EVENTS)
    return llvm::JITEventListener::createPerfJITEventListener();
#else
    return nullptr;
#endif
}

std::string ToString(llvm::Error error) {
    return llvm::toString(std::move(error));
}

template <typename T>
std::optional<T> TakeExpected(llvm::Expected<T> expected, std::string* error) {
    if (!expected) {
        if (error) {
            *error = ToString(expected.takeError());
        } else {
            llvm::consumeError(expected.takeError());
        }
        return std::nullopt;
    }
    return std::move(*expected);
}

class TDiskObjectCache final : public llvm::ObjectCache {
public:
    TDiskObjectCache(std::string dir, bool nativeCode)
        : Dir_(std::move(dir))
        , NativeCode_(nativeCode)
    {}

    void notifyObjectCompiled(
        const llvm::Module* module,
        llvm::MemoryBufferRef object) override
    {
        llvm::SmallString<256> path = CachePath(CacheKeyForCompiledModule(module));
        llvm::SmallString<256> model = path;
        model += ".%%%%%%.tmp";

        int fd = -1;
        llvm::SmallString<256> tempPath;
        if (llvm::sys::fs::createUniqueFile(model, fd, tempPath)) {
            return;
        }

        llvm::raw_fd_ostream out(fd, true);
        out.write(object.getBufferStart(), object.getBufferSize());
        out.close();
        if (out.has_error()) {
            (void)llvm::sys::fs::remove(tempPath);
            return;
        }

        if (llvm::sys::fs::rename(tempPath, path)) {
            (void)llvm::sys::fs::remove(tempPath);
        }
    }

    std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module* module) override {
        std::string key = CacheKey(module);
        Keys_[module] = key;
        auto buffer = llvm::MemoryBuffer::getFile(CachePath(key), false, false);
        if (!buffer) {
            return nullptr;
        }
        return std::move(*buffer);
    }

private:
    std::string CacheKey(const llvm::Module* module) const {
        llvm::SHA256 hash;
        hash.update("qumir-orc-object-cache-v1");
        hash.update(LLVM_VERSION_STRING);
        hash.update(module->getTargetTriple().str());
        hash.update(module->getDataLayout().getStringRepresentation());
        if (NativeCode_) {
            auto [nativeCpu, nativeFeatures] = GetNativeCpuAndFeatures();
            hash.update(nativeCpu);
            hash.update(nativeFeatures);
        } else {
            hash.update("generic");
        }

        std::string ir;
        llvm::raw_string_ostream out(ir);
        module->print(out, nullptr);
        out.flush();
        hash.update(ir);

        auto digest = hash.final();
        return llvm::toHex(
            llvm::ArrayRef<uint8_t>(digest.data(), digest.size()),
            true);
    }

    std::string CacheKeyForCompiledModule(const llvm::Module* module) {
        auto it = Keys_.find(module);
        if (it != Keys_.end()) {
            return it->second;
        }
        return CacheKey(module);
    }

    llvm::SmallString<256> CachePath(const std::string& key) const {
        llvm::SmallString<256> path(Dir_);
        llvm::sys::path::append(path, key + ".o");
        return path;
    }

    std::string Dir_;
    bool NativeCode_ = false;
    std::unordered_map<const llvm::Module*, std::string> Keys_;
};

std::unique_ptr<llvm::ObjectCache> CreateDiskObjectCache(bool nativeCode) {
    const char* dir = std::getenv("QUMIR_LLVM_OBJECT_CACHE_DIR");
    if (!dir || !*dir) {
        return nullptr;
    }
    if (llvm::sys::fs::create_directories(dir)) {
        return nullptr;
    }
    return std::make_unique<TDiskObjectCache>(dir, nativeCode);
}

std::unique_ptr<llvm::orc::LLJIT> CreateOrcJit(
    bool nativeCode,
    llvm::ObjectCache* objectCache,
    std::string* error)
{
    auto jtmb = TakeExpected(llvm::orc::JITTargetMachineBuilder::detectHost(), error);
    if (!jtmb) {
        return nullptr;
    }

    jtmb->setRelocationModel(llvm::Reloc::PIC_);
    if (nativeCode) {
        auto [nativeCpu, nativeFeatures] = GetNativeCpuAndFeatures();
        if (!nativeCpu.empty()) {
            jtmb->setCPU(nativeCpu);
        }
        if (!nativeFeatures.empty()) {
            jtmb->addFeatures(SplitFeatures(nativeFeatures));
        }
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(*jtmb));
    if (objectCache) {
        builder.setCompileFunctionCreator(
            [objectCache](llvm::orc::JITTargetMachineBuilder jtmb)
                -> llvm::Expected<std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>>
            {
                auto tm = jtmb.createTargetMachine();
                if (!tm) {
                    return tm.takeError();
                }

                std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler> compiler =
                    std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
                        std::move(*tm),
                        objectCache);
                return compiler;
            });
    }
    auto jit = TakeExpected(builder.create(), error);
    if (!jit) {
        return nullptr;
    }
    return std::move(*jit);
}

} // namespace

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

TLlvmRunner::TLlvmRunner(TLlvmRunnerOptions options)
    : Options_(std::move(options))
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
    if (artifacts->NativeCode) {
        auto [nativeCpu, nativeFeatures] = GetNativeCpuAndFeatures();
        if (!nativeCpu.empty()) {
            builder.setMCPU(nativeCpu);
        }
        if (!nativeFeatures.empty()) {
            builder.setMAttrs(SplitFeatures(nativeFeatures));
        }
    }
    llvm::JITEventListener* perfListener = nullptr;
    auto ee = std::unique_ptr<llvm::ExecutionEngine>(builder.setErrorStr(&eeErr).create());
    if (!ee) {
        if (error) *error = std::string("ExecutionEngine create failed: ") + eeErr;
        return std::nullopt;
    }
    if (Options_.EnablePerfJitEventListener) {
        perfListener = CreatePerfJitEventListener();
        if (perfListener) {
            ee->RegisterJITEventListener(perfListener);
        }
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
        processed += NRuntime::drawer_process_events();
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

void* TLlvmRunner::Lookup(
    std::unique_ptr<ILLVMModuleArtifacts> iartifacts,
    const std::string& name,
    std::string* error)
{
    auto entries = LookupMany(std::move(iartifacts), {name}, error);
    auto it = entries.find(name);
    return it == entries.end() ? nullptr : it->second;
}

std::unordered_map<std::string, void*> TLlvmRunner::LookupMany(
    std::unique_ptr<ILLVMModuleArtifacts> iartifacts,
    const std::vector<std::string>& names,
    std::string* error)
{
    auto* artifacts = static_cast<TLLVMModuleArtifacts*>(iartifacts.get());
    if (!artifacts || !artifacts->Module) {
        if (error) *error = "null artifacts";
        return {};
    }

    static bool inited = false;
    if (!inited) {
        LLVMInitializeNativeTarget();
        LLVMInitializeNativeAsmPrinter();
        LLVMInitializeNativeAsmParser();
        inited = true;
    }

    llvm::sys::DynamicLibrary::LoadLibraryPermanently(nullptr);

    auto objectCache = CreateDiskObjectCache(artifacts->NativeCode);
    auto jit = CreateOrcJit(artifacts->NativeCode, objectCache.get(), error);
    if (!jit) {
        return {};
    }
    artifacts->Module->setDataLayout(jit->getDataLayout());
    auto module = llvm::orc::ThreadSafeModule(
        std::move(artifacts->Module),
        std::move(artifacts->Ctx));
    if (auto err = jit->addIRModule(std::move(module))) {
        if (error) {
            *error = ToString(std::move(err));
        }
        return {};
    }

    std::unordered_map<std::string, void*> entries;
    entries.reserve(names.size());
    for (const auto& name : names) {
        auto addr = jit->lookup(name);
        if (!addr) {
            if (error) {
                *error = ToString(addr.takeError());
            } else {
                llvm::consumeError(addr.takeError());
            }
            return {};
        }
        entries.emplace(name, addr->toPtr<void*>());
    }

    struct TLiveJit {
        std::unique_ptr<llvm::ObjectCache> ObjectCache;
        std::unique_ptr<llvm::orc::LLJIT> Jit;
    };

    auto live = std::make_shared<TLiveJit>();
    live->ObjectCache = std::move(objectCache);
    live->Jit = std::move(jit);
    LiveEngines_.push_back(std::move(live));
    return entries;
}

} // namespace NQumir::NCodeGen
