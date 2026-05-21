#include "future.h"

#include <coroutine>
#include <cstddef>

namespace NQumir {

namespace {

struct TWrappedLLVMCoro : ITypeErasedFuture {
    std::coroutine_handle<> Handle;
    size_t ResultSize;

    bool done() override {
        return Handle.done();
    }

    void resume() override {
        if (!Handle.done()) Handle.resume();
    }

    void destroy() override {
        Handle.destroy();
    }

    void* address() override {
        return Handle.address();
    }

    bool await_ready() override {
        return done();
    }

    void* await_suspend(void* caller) override {
        // Drive child one step; parent polls via lowerAwaitFuture loop.
        if (!Handle.done()) {
            Handle.resume();
        }
        return nullptr; // noop — parent suspends and re-checks await_ready
    }

    void await_resume(void* /*result*/) override {
        // Result is extracted by lowerAwaitFuture via llvm.coro.promise directly.
    }
};

} // namespace

extern "C" {

void __qumir_future_destroy(ITypeErasedFuture* future) {
    future->destroy();
    delete future;
}

bool __qumir_future_done(ITypeErasedFuture* future) {
    return future->done();
}

void __qumir_future_resume(ITypeErasedFuture* future) {
    future->resume();
}

void* __qumir_future_address(ITypeErasedFuture* future) {
    return future->address();
}

bool __qumir_future_await_ready(ITypeErasedFuture* future) {
    return future->await_ready();
}

void* __qumir_future_await_suspend(ITypeErasedFuture* future, void* caller) {
    return future->await_suspend(caller);
}

void __qumir_future_await_resume(ITypeErasedFuture* future, void* result) {
    future->await_resume(result);
}

ITypeErasedFuture* __qumir_wrap_coro(void* handle, size_t result_size) {
    auto* w = new TWrappedLLVMCoro();
    w->Handle = std::coroutine_handle<>::from_address(handle);
    w->ResultSize = result_size;
    return w;
}

} // extern "C"


} // namespace NQumir
