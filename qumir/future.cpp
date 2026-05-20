#include "future.h"

namespace NQumir {

extern "C" {

void __qumir_future_destroy(ITypeErasedFuture* future) {
    future->destroy();
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

void __qumir_future_await_suspend(ITypeErasedFuture* future, void* caller) {
    future->await_suspend(std::coroutine_handle<>::from_address(caller));
}

void __qumir_future_await_resume(ITypeErasedFuture* future, void* result) {
    future->await_resume(result);
}

} // extern "C"
} // namespace NQumir
