#include "ffi.h"

#include <cassert>
#include <cstring>
#include <new>
#include <utility>

namespace NQumir {
namespace NIR {
namespace NFFI {

namespace {

static_assert(sizeof(void*) == sizeof(int64_t));

// Same ABI register classification as the real struct it stands in for.
struct TSInt {
    int64_t a;
};
struct TSSse {
    double a;
};
struct TSIntInt {
    int64_t a;
    int64_t b;
};
struct TSIntSse {
    int64_t a;
    double b;
};
struct TSSseInt {
    double a;
    int64_t b;
};
struct TSSseSse {
    double a;
    double b;
};

template<typename ReturnType, typename... Types>
struct TFunction : public IFunction {
    using TFunctionType = ReturnType(*)(Types...);

    TFunction(void* addr)
        : Symbol(reinterpret_cast<TFunctionType>(addr))
    { }

    TFunction(void* addr, size_t returnSize)
        : Symbol(reinterpret_cast<TFunctionType>(addr))
        , ReturnSize(returnSize)
    { }

    uint64_t operator() (const uint64_t* args, size_t argCount) override {
        if (ReturnSize != 0) {
            assert(argCount == sizeof...(Types) + 1);
            return Call(args + 1, reinterpret_cast<void*>(static_cast<uintptr_t>(args[0])), std::index_sequence_for<Types...>{});
        }

        assert(argCount == sizeof...(Types));
        return Call(args, nullptr, std::index_sequence_for<Types...>{});
    }

    template<size_t... I>
    uint64_t Call(const uint64_t* args, void* ret, std::index_sequence<I...>) {
        if constexpr(std::is_same_v<ReturnType, void>) {
            Symbol(LoadArg<Types>(args[I])...);
            return 0;
        } else {
            ReturnType result = Symbol(LoadArg<Types>(args[I])...);
            return StoreRet(result, ret, ReturnSize);
        }
    }

    TFunctionType Symbol = nullptr;
    size_t ReturnSize = 0;
};

template<typename F>
bool DispatchScalar(EKind k, F&& f) {
    switch (k) {
        case EKind::I1:
        case EKind::I8:
        case EKind::U8:
        case EKind::I16:
        case EKind::U16:
        case EKind::I32:
        case EKind::U32:
        case EKind::I64:
        case EKind::U64:
        case EKind::Ptr:
            f.template operator()<int64_t>();
            return true;
        case EKind::F64:
            f.template operator()<double>();
            return true;
        default:
            return false;
    }
}

template<typename F>
bool DispatchStruct(EStructKind sk, F&& f) {
    switch (sk) {
        case EStructKind::Memory:
            f.template operator()<void*>();
            return true;
        case EStructKind::Int:
            f.template operator()<TSInt>();
            return true;
        case EStructKind::Sse:
            f.template operator()<TSSse>();
            return true;
        case EStructKind::IntInt:
            f.template operator()<TSIntInt>();
            return true;
        case EStructKind::IntSse:
            f.template operator()<TSIntSse>();
            return true;
        case EStructKind::SseInt:
            f.template operator()<TSSseInt>();
            return true;
        case EStructKind::SseSse:
            f.template operator()<TSSseSse>();
            return true;
        default:
            return false;
    }
}

template<typename F>
bool DispatchArg(EKind k, EStructKind sk, F&& f) {
    if (k == EKind::Struct) {
        return DispatchStruct(sk, std::forward<F>(f));
    }
    return DispatchScalar(k, std::forward<F>(f));
}

template<typename F>
bool DispatchRet(EKind k, F&& f) {
    if (k == EKind::Void) {
        f.template operator()<void>();
        return true;
    }
    return DispatchScalar(k, std::forward<F>(f));
}

template<typename Finish>
IFunction* DispatchArgs(
    const std::vector<EKind>& ak,
    const std::vector<EStructKind>& as,
    Finish&& finish)
{
    if (ak.size() != as.size()) {
        return nullptr;
    }

    IFunction* r = nullptr;
    switch (ak.size()) {
    case 0:
        r = finish.template operator()<>();
        break;
    case 1:
        if (!DispatchArg(ak[0], as[0], [&]<class T0>() {
            r = finish.template operator()<T0>();
        })) {
            return nullptr;
        }
        break;
    case 2:
        if (!DispatchArg(ak[0], as[0], [&]<class T0>() {
            if (!DispatchArg(ak[1], as[1], [&]<class T1>() {
                r = finish.template operator()<T0, T1>();
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    case 3:
        if (!DispatchArg(ak[0], as[0], [&]<class T0>() {
            if (!DispatchArg(ak[1], as[1], [&]<class T1>() {
                if (!DispatchArg(ak[2], as[2], [&]<class T2>() {
                    r = finish.template operator()<T0, T1, T2>();
                })) {
                    r = nullptr;
                }
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    default:
        return nullptr;
    }
    return r;
}

} // namespace

IFunction* BuildFFI(
    void* symbol,
    EKind retKind,
    EStructKind retStruct,
    size_t,
    const std::vector<EKind>& argKinds,
    const std::vector<EStructKind>& argStructs) noexcept
{
    if (retKind == EKind::Struct && retStruct == EStructKind::Memory) {
        return nullptr;
    }

    if (retKind == EKind::Struct) {
        IFunction* r = nullptr;
        if (!DispatchStruct(retStruct, [&]<class Ret>() {
            r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
                return new (std::nothrow) TFunction<Ret, Args...>(symbol, sizeof(Ret));
            });
        })) {
            return nullptr;
        }
        return r;
    }

    IFunction* r = nullptr;
    if (!DispatchRet(retKind, [&]<class Ret>() {
        r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
            return new (std::nothrow) TFunction<Ret, Args...>(symbol);
        });
    })) {
        return nullptr;
    }
    return r;
}

} // namespace NFFI
} // namespace NIR
} // namespace NQumir
