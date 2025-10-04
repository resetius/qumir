#pragma once

#include <optional>
#include <coroutine>
#include <exception>
#include <expected>
#include <type_traits>
#include <concepts>

namespace NOz {

struct TNonePropagate : std::exception {
    const char* what() const noexcept override {
        return "end of stream";
    }
};

template<typename T>
struct TOptAwaitable {
    std::optional<T> Value;
    bool await_ready() const noexcept {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept
    { }

    T await_resume() {
        if (!Value) {
            throw TNonePropagate {};
        }
        return std::move(*Value);
    }
};

template<typename T>
struct TOptTask {
    struct promise_type {
        std::optional<T> Value;

        TOptTask get_return_object() {
            return TOptTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() const noexcept {
            return {};
        }

        std::suspend_always final_suspend() const noexcept {
            return {};
        }

        void return_value(T v) {
            Value = std::move(v);
        }

        template<typename U>
        auto await_transform(std::optional<U> opt) {
            return TOptAwaitable<U>{std::move(opt)};
        }

        template<typename U>
        auto await_transform(TOptTask<U> task) {
            return TOptAwaitable<U>{task.result()};
        }

        void unhandled_exception() {
            try {
                throw;
            } catch (const TNonePropagate&) {
                Value = std::nullopt;
            } catch (...) {
                std::terminate();
            }
        }
    };

    ~TOptTask() {
        if (Handle) {
            Handle.destroy();
        }
    }

    std::optional<T> result() {
        return std::move(Handle.promise().Value);
    }

    std::coroutine_handle<promise_type> Handle;
};

template<typename T, typename E>
struct TExpectedAwaitable
{
    std::expected<T, E> Value;

    bool await_ready() const noexcept {
        return true;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept
    { }

    T await_resume() {
        if (Value.has_value()) {
            return std::move(*Value);
        } else {
            throw Value.error();
        }
    }
};

template<typename T, typename E, typename Loc = void>
struct TExpectedTask
{
    struct promise_type {
        std::expected<T, E> Value;

        static constexpr bool HasLoc = !std::is_void_v<Loc>;
        using MaybeLoc = std::conditional_t<HasLoc, std::optional<Loc>, std::monostate>;
        MaybeLoc Ctx{};

        promise_type() = default;

        // Detect arguments that can provide Loc directly, via implicit conversion, or via operator() -> Loc
        template<class A>
        static constexpr bool ProvidesLoc =
            std::same_as<std::remove_cvref_t<A>, Loc> ||
            std::convertible_to<A, Loc> ||
            requires (A a) { { a() } -> std::convertible_to<Loc>; };

        template<class... Args>
        requires (HasLoc && (ProvidesLoc<Args> || ...))
        promise_type(Args&&... args) {
            std::optional<Loc> found;
            auto pick = [&](auto&& a) {
                using A = std::remove_cvref_t<decltype(a)>;
                if constexpr (std::same_as<A, Loc>) {
                    found = a;
                } else if constexpr (std::convertible_to<A, Loc>) {
                    found = static_cast<Loc>(a);
                } else if constexpr (requires { { a() } -> std::convertible_to<Loc>; }) {
                    found = a();
                }
            };
            (pick(args), ...);
            if (found) {
                Ctx.emplace(*found);
            }
        }

        TExpectedTask get_return_object() {
            return TExpectedTask{
                std::coroutine_handle<promise_type>::from_promise(*this)
            };
        }

        std::suspend_never initial_suspend() const noexcept {
            return {};
        }

        std::suspend_always final_suspend() const noexcept {
            return {};
        }

        void return_value(T v) {
            Value = std::move(v);
        }

        void return_value(E e) {
            Value = std::unexpected{std::move(e)};
        }

        template<typename U, typename X>
        auto await_transform(std::expected<U, X> exp) {
            return TExpectedAwaitable<U, X>{std::move(exp)};
        }

        template<typename U, typename X>
        auto await_transform(TExpectedTask<U, X, Loc> task) {
            return TExpectedAwaitable<U, X>{task.result()};
        }

        template<typename U>
        auto await_transform(std::optional<U> opt) {
            return TOptAwaitable<U>{std::move(opt)};
        }

        template<typename X>
        auto await_transform(std::unexpected<X> unexp) {
            return TExpectedAwaitable<T, X>{std::expected<T, X>{std::move(unexp)}};
        }

        void unhandled_exception() {
            try {
                throw;
            } catch (const std::exception& exc) {
                if constexpr (HasLoc) {
                    if (Ctx) {
                        Value = std::unexpected{E(*Ctx, exc)};
                    } else {
                        Value = std::unexpected{E(exc)};
                    }
                } else {
                    Value = std::unexpected{E(exc)};
                }
            } catch (...) {
                std::terminate();
            }
        }
    };

    ~TExpectedTask() {
        if (Handle) {
            Handle.destroy();
        }
    }

    std::expected<T, E> result() {
        return std::move(Handle.promise().Value);
    }

    std::coroutine_handle<promise_type> Handle;
};

} // namespace NOz

