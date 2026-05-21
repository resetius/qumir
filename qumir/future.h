#pragma once

#include <coroutine>
#include <expected>
#include <optional>
#include <exception>
#include <utility>
#include <cassert>
#include <memory>
#include <type_traits>

#include <qumir/runtime/future.h>

namespace NQumir {

template<typename T> struct TFinalAwaiter;

template<typename T> struct TFuture;

template<typename T>
struct TPromiseBase {
    std::suspend_never initial_suspend() { return {}; }
    TFinalAwaiter<T> final_suspend() noexcept;
    /// Handle to the caller coroutine (initialized to a no-operation coroutine).
    std::coroutine_handle<> Caller = std::noop_coroutine();
};

template<typename T>
struct TPromise: public TPromiseBase<T> {
    TFuture<T> get_return_object();

    void return_value(const T& t) {
        ErrorOr = t;
    }

    void return_value(T&& t) {
        ErrorOr = std::move(t);
    }

    void unhandled_exception() {
        ErrorOr = std::unexpected(std::current_exception());
    }

    /// Optional container that holds either the result or an exception.
    std::optional<std::expected<T, std::exception_ptr>> ErrorOr;
};

template<typename T>
struct TFutureBase {
    using THandle = std::coroutine_handle<TPromise<T>>;

    TFutureBase() = default;
    TFutureBase(TPromise<T>& promise)
        : Coro(THandle::from_promise(promise))
    { }
    explicit TFutureBase(std::shared_ptr<TPromise<T>> promise)
        : ExternalPromise(std::move(promise))
    { }
    TFutureBase(TFutureBase&& other)
        : Coro(std::exchange(other.Coro, nullptr))
        , ExternalPromise(std::move(other.ExternalPromise))
    { }
    TFutureBase(const TFutureBase&) = delete;
    TFutureBase& operator=(const TFutureBase&) = delete;
    TFutureBase& operator=(TFutureBase&& other) = delete;

    ~TFutureBase() {
        if (Coro) {
            Coro.destroy();
        }
    }

    explicit operator bool() const {
        return Coro || ExternalPromise;
    }

    bool done() const {
        return Promise().ErrorOr.has_value();
    }

    void resume() {
        if (Coro) {
            if (!Coro.done()) {
                Coro.resume();
            }
            return;
        }
        if constexpr(std::is_same_v<T, void>) {
            if (ExternalPromise && !ExternalPromise->ErrorOr.has_value()) {
                ExternalPromise->return_void();
                ResumeCaller();
            }
        } else {
            assert(false && "external non-void future needs an explicit result");
        }
    }

    void* address() const {
        return Coro ? Coro.address() : nullptr;
    }

    bool await_ready() const {
        return Promise().ErrorOr.has_value();
    }

    void destroy() {
        if (Coro) {
            Coro.destroy();
            Coro = nullptr;
        }
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
        Promise().Caller = caller;
        return std::noop_coroutine();
    }

    TPromise<T>& Promise() {
        return Coro ? Coro.promise() : *ExternalPromise;
    }

    const TPromise<T>& Promise() const {
        return Coro ? Coro.promise() : *ExternalPromise;
    }

    using promise_type = TPromise<T>;

private:
    void ResumeCaller() {
        auto caller = ExternalPromise->Caller;
        ExternalPromise->Caller = std::noop_coroutine();
        if (caller) {
            caller.resume();
        }
    }

    THandle Coro = nullptr;
    std::shared_ptr<TPromise<T>> ExternalPromise;
};

template<> struct TFuture<void>;

template<typename T>
struct TFuture : public TFutureBase<T> {
    using TFutureBase<T>::TFutureBase;

    T await_resume() {
        auto& errorOr = *this->Promise().ErrorOr;
        if (errorOr.has_value()) {
            return std::move(errorOr.value());
        } else {
            std::rethrow_exception(errorOr.error());
        }
    }
};

template<>
struct TPromise<void>: public TPromiseBase<void> {
    TFuture<void> get_return_object();

    void return_void() {
        ErrorOr = nullptr;
    }

    void unhandled_exception() {
        ErrorOr = std::current_exception();
    }

    std::optional<std::exception_ptr> ErrorOr;
};


template<>
struct TFuture<void> : public TFutureBase<void> {
    using TFutureBase<void>::TFutureBase;

    void await_resume() {
        auto& errorOr = *this->Promise().ErrorOr;
        if (errorOr) {
            std::rethrow_exception(errorOr);
        }
    }
};

template<typename T>
struct TFinalAwaiter {
    bool await_ready() noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<TPromise<T>> h) noexcept {
        return h.promise().Caller;
    }
    void await_resume() noexcept { }
};

inline TFuture<void> TPromise<void>::get_return_object() { return { TFuture<void>{*this} }; }
template<typename T>
TFuture<T> TPromise<T>::get_return_object() { return { TFuture<T>{*this} }; }


template<typename T>
TFinalAwaiter<T> TPromiseBase<T>::final_suspend() noexcept { return {}; }

template<typename T>
TFuture<T> MakeExternalFuture(const std::shared_ptr<TPromise<T>>& promise) {
    return TFuture<T>(promise);
}

template<typename T>
struct TWrappedFuture : public ITypeErasedFuture {
    TWrappedFuture(TFuture<T>&& future)
        : Future(std::move(future))
    { }

    bool done() override {
        assert(Future);
        return Future->done();
    }

    void resume() override {
        assert(Future);
        if (!Future->done()) {
            Future->resume();
        }
    }

    void destroy() override {
        Future->destroy();
    }

    bool await_ready() override {
        assert(Future);
        return Future->await_ready();
    }

    void* await_suspend(void* caller) override {
        assert(Future);
        return Future->await_suspend(std::coroutine_handle<>::from_address(caller)).address();
    }

    void await_resume(void* result) override {
        assert(Future);
        if constexpr(std::is_same_v<T, void>) {
            Future->await_resume();
        } else {
            new (result) T(Future->await_resume());
        }
    }

    void* address() override {
        assert(Future);
        return Future->address();
    }

private:
    std::optional<TFuture<T>> Future;
};

template<typename T>
struct TTypeErasedAwaiter {
    ITypeErasedFuture* Future;

    bool await_ready() {
        return Future->await_ready();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return std::coroutine_handle<>::from_address(Future->await_suspend(h.address()));
    }

    T await_resume() {
        T result;
        Future->await_resume(&result);
        return result;
    }
};

template<>
struct TTypeErasedAwaiter<void> {
    ITypeErasedFuture* Future;

    bool await_ready() {
        return Future->await_ready();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        return std::coroutine_handle<>::from_address(Future->await_suspend(h.address()));
    }

    void await_resume() {
        Future->await_resume(nullptr);
    }
};

template<typename T>
TFuture<T> AwaitTypeErasedFuture(ITypeErasedFuture* erasedFuture) {
    struct TGuard {
        ITypeErasedFuture* Future;
        ~TGuard() {
            if (Future) {
                Future->destroy();
                delete Future;
            }
        }
    } guard{ erasedFuture };
    if constexpr(std::is_same_v<T, void>) {
        co_await TTypeErasedAwaiter<void>{erasedFuture};
        co_return;
    } else {
        T result = co_await TTypeErasedAwaiter<T>{erasedFuture};
        co_return result;
    }
}

} // namespace NQumir
