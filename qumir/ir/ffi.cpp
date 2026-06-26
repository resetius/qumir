#include "ffi.h"

#include <array>
#include <cassert>
#include <cstring>
#include <new>
#include <utility>

#if !defined(__x86_64__) && !defined(_M_X64) && !defined(__aarch64__) && !defined(_M_ARM64)
#error "NFFI struct argument decomposition supports only x86-64 and AArch64"
#endif

namespace NQumir {
namespace NIR {
namespace NFFI {

namespace {

static_assert(sizeof(void*) == sizeof(int64_t));

// Captures a 2-eightbyte struct return value (a single object held in two
// registers); only return values still need a concrete struct type.
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

// Where one native eightbyte comes from: a VM argument slot, optionally
// dereferenced (struct passed by pointer) at a field offset. Struct arguments
// expand to one such source per eightbyte so the thunk only ever instantiates
// int64_t / double argument types.
struct TFlatSrc {
    uint8_t VmIndex;
    uint8_t Offset;
    bool Deref;
};

constexpr size_t MaxFlatArgs = 8;

std::array<TFlatSrc, MaxFlatArgs> BuildFlatSrc(
    const std::vector<EKind>& argKinds,
    const std::vector<EStructKind>& argStructs,
    size_t& flatCount)
{
    std::array<TFlatSrc, MaxFlatArgs> srcs{};
    flatCount = 0;
    auto push = [&](uint8_t vm, uint8_t offset, bool deref) {
        if (flatCount < MaxFlatArgs) {
            srcs[flatCount] = {vm, offset, deref};
        }
        ++flatCount;
    };
    for (size_t i = 0; i < argKinds.size() && i < argStructs.size(); ++i) {
        auto vm = static_cast<uint8_t>(i);
        if (argKinds[i] != EKind::Struct) {
            push(vm, 0, false);
            continue;
        }
        switch (argStructs[i]) {
            case EStructKind::Memory:
                push(vm, 0, false);
                break;
            case EStructKind::Int:
            case EStructKind::Sse:
                push(vm, 0, true);
                break;
            case EStructKind::IntInt:
            case EStructKind::IntSse:
            case EStructKind::SseInt:
            case EStructKind::SseSse:
                push(vm, 0, true);
                push(vm, 8, true);
                break;
            default:
                break;
        }
    }
    return srcs;
}

template<typename ReturnType, typename... Types>
struct TFunction : public IFunction {
    using TFunctionType = ReturnType(*)(Types...);

    TFunction(void* addr, size_t returnSize, const std::array<TFlatSrc, MaxFlatArgs>& srcs)
        : Symbol(reinterpret_cast<TFunctionType>(addr))
        , ReturnSize(returnSize)
        , Srcs(srcs)
    { }

    uint64_t operator() (const uint64_t* args, size_t) override {
        void* ret = nullptr;
        const uint64_t* vmArgs = args;
        if (ReturnSize != 0) {
            ret = reinterpret_cast<void*>(static_cast<uintptr_t>(args[0]));
            vmArgs = args + 1;
        }

        std::array<uint64_t, sizeof...(Types)> flat{};
        for (size_t j = 0; j < sizeof...(Types); ++j) {
            uint64_t base = vmArgs[Srcs[j].VmIndex];
            if (Srcs[j].Deref) {
                memcpy(&flat[j], reinterpret_cast<const char*>(static_cast<uintptr_t>(base)) + Srcs[j].Offset, sizeof(uint64_t));
            } else {
                flat[j] = base;
            }
        }
        return Call(flat.data(), ret, std::index_sequence_for<Types...>{});
    }

    template<size_t... I>
    uint64_t Call(const uint64_t* flat, void* ret, std::index_sequence<I...>) {
        if constexpr(std::is_same_v<ReturnType, void>) {
            Symbol(LoadArg<Types>(flat[I])...);
            return 0;
        } else {
            ReturnType result = Symbol(LoadArg<Types>(flat[I])...);
            return StoreRet(result, ret, ReturnSize);
        }
    }

    TFunctionType Symbol = nullptr;
    size_t ReturnSize = 0;
    std::array<TFlatSrc, MaxFlatArgs> Srcs{};
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

// A struct return is captured as one value, so it still needs a concrete type;
// single-eightbyte and pointer returns reuse int64_t / double.
template<typename F>
bool DispatchStructRet(EStructKind sk, F&& f) {
    switch (sk) {
        case EStructKind::Int:
            f.template operator()<int64_t>();
            return true;
        case EStructKind::Sse:
            f.template operator()<double>();
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

// A struct argument decomposes into the int64_t / double of each eightbyte; the
// matching value sources are produced by BuildFlatSrc.
template<typename F>
bool DispatchArgExpand(EKind k, EStructKind sk, F&& f) {
    if (k != EKind::Struct) {
        return DispatchScalar(k, std::forward<F>(f));
    }
    switch (sk) {
        case EStructKind::Memory:
        case EStructKind::Int:
            f.template operator()<int64_t>();
            return true;
        case EStructKind::Sse:
            f.template operator()<double>();
            return true;
        case EStructKind::IntInt:
            f.template operator()<int64_t, int64_t>();
            return true;
        case EStructKind::SseSse:
            f.template operator()<double, double>();
            return true;
        // Mixed structs differ by ABI: x86-64 routes each eightbyte to its own
        // register file (GP + SSE), AArch64 keeps a non-HFA struct entirely in
        // GP registers, so the SSE eightbyte travels as raw int64 bits.
        case EStructKind::IntSse:
#if defined(__x86_64__) || defined(_M_X64)
            f.template operator()<int64_t, double>();
#else
            f.template operator()<int64_t, int64_t>();
#endif
            return true;
        case EStructKind::SseInt:
#if defined(__x86_64__) || defined(_M_X64)
            f.template operator()<double, int64_t>();
#else
            f.template operator()<int64_t, int64_t>();
#endif
            return true;
        default:
            return false;
    }
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
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            r = finish.template operator()<A0...>();
        })) {
            return nullptr;
        }
        break;
    case 2:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                r = finish.template operator()<A0..., A1...>();
            })) {
                r = nullptr;
            }
        })) {
            return nullptr;
        }
        break;
    case 3:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                if (!DispatchArgExpand(ak[2], as[2], [&]<class... A2>() {
                    r = finish.template operator()<A0..., A1..., A2...>();
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
    case 4:
        if (!DispatchArgExpand(ak[0], as[0], [&]<class... A0>() {
            if (!DispatchArgExpand(ak[1], as[1], [&]<class... A1>() {
                if (!DispatchArgExpand(ak[2], as[2], [&]<class... A2>() {
                    if (!DispatchArgExpand(ak[3], as[3], [&]<class... A3>() {
                        r = finish.template operator()<A0..., A1..., A2..., A3...>();
                    })) {
                        r = nullptr;
                    }
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

    size_t flatCount = 0;
    std::array<TFlatSrc, MaxFlatArgs> srcs = BuildFlatSrc(argKinds, argStructs, flatCount);
    if (flatCount > MaxFlatArgs) {
        return nullptr;
    }

    if (retKind == EKind::Struct) {
        IFunction* r = nullptr;
        if (!DispatchStructRet(retStruct, [&]<class Ret>() {
            r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
                return new (std::nothrow) TFunction<Ret, Args...>(symbol, sizeof(Ret), srcs);
            });
        })) {
            return nullptr;
        }
        return r;
    }

    IFunction* r = nullptr;
    if (!DispatchRet(retKind, [&]<class Ret>() {
        r = DispatchArgs(argKinds, argStructs, [&]<class... Args>() -> IFunction* {
            return new (std::nothrow) TFunction<Ret, Args...>(symbol, 0, srcs);
        });
    })) {
        return nullptr;
    }
    return r;
}

} // namespace NFFI
} // namespace NIR
} // namespace NQumir
