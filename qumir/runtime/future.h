#pragma once

#include <cstddef>

namespace NQumir {

struct ITypeErasedFuture {
    virtual ~ITypeErasedFuture() = default;

    virtual bool done() = 0;
    virtual void resume() = 0;
    virtual void destroy() = 0;
    virtual void* address() = 0;

    virtual bool await_ready() = 0;
    virtual void* await_suspend(void* caller) = 0;
    virtual void await_resume(void* result) = 0;
};

extern "C" {

void __qumir_future_destroy(ITypeErasedFuture* future);
bool __qumir_future_done(ITypeErasedFuture* future);
void __qumir_future_resume(ITypeErasedFuture* future);
void* __qumir_future_address(ITypeErasedFuture* future);
bool __qumir_future_await_ready(ITypeErasedFuture* future);
void* __qumir_future_await_suspend(ITypeErasedFuture* future, void* caller);
void __qumir_future_await_resume(ITypeErasedFuture* future, void* result);
ITypeErasedFuture* __qumir_wrap_coro(void* handle, size_t result_size);

} // extern "C"


} // namespace NQumir
