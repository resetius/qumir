#include "ffi.h"

#include <cassert>
#include <stdexcept>

namespace NQumir {
namespace NIR {
namespace NFFI {

template<typename ReturnType, typename... Types>
struct TFunction : public IFunction {
    using TFunctionType = ReturnType(*)(Types...);

    TFunction(void* addr)
        : Symbol(reinterpret_cast<TFunctionType>(addr))
    { }

    uint64_t operator() (const uint64_t* args, size_t argCount) {
        assert(argCount == sizeof...(Types));
        return Call(args, std::index_sequence_for<Types...>{});
    }

    template<size_t... I>
    uint64_t Call(const uint64_t* args, std::index_sequence<I...>) {
        if constexpr(std::is_same_v<ReturnType, void>) {
            Symbol(LoadArg<Types>(args[I])...);
        } else {
            auto ret = Symbol(LoadArg<Types>(args[I])...);
            return StoreRet(ret);
        }
    }

    TFunctionType Symbol = nullptr;
};

template<typename F>
decltype(auto) Call(EKind k, F&& f) {
    switch (k) {
        case EKind::I1:
            f.template operator()<bool>();
            break;
        case EKind::I8:
            f.template operator()<int8_t>();
            break;
        case EKind::I16:
            f.template operator()<int16_t>();
            break;
        case EKind::I32:
            f.template operator()<int32_t>();
            break;
        case EKind::I64:
            f.template operator()<int64_t>();
            break;

        case EKind::U8:
            f.template operator()<uint8_t>();
            break;
        case EKind::U16:
            f.template operator()<uint16_t>();
            break;
        case EKind::U32:
            f.template operator()<uint32_t>();
            break;
        case EKind::U64:
            f.template operator()<uint64_t>();
            break;

        case EKind::F32:
            f.template operator()<float>();
            break;
        case EKind::F64:
            f.template operator()<double>();
            break;

        case EKind::Ptr:
            f.template operator()<void*>();
            break;

        case EKind::Struct:
            // TODO: need to use size for ABI?
        default:
            throw std::runtime_error("Unsupported FFI type");
    }
}

IFunction* Builder0(void* symbol, EKind k0) {
    IFunction* ret = nullptr;
    Call(k0, [&]<typename T0>() {
        ret = new TFunction<T0>(symbol);
    });
    return ret;
}

IFunction* Builder1(void* symbol, EKind k0, EKind k1) {
    IFunction* ret = nullptr;
    Call(k0, [&]<typename T0>() {
        Call(k1, [&]<typename T1>() {
            ret = new TFunction<T0, T1>(symbol);
        });
    });
    return ret;
}

IFunction* Builder2(void* symbol, EKind k0, EKind k1, EKind k2) {
    IFunction* ret = nullptr;
    Call(k0, [&]<typename T0>() {
        Call(k1, [&]<typename T1>() {
            Call(k2, [&]<typename T2>() {
                ret = new TFunction<T0, T1, T2>(symbol);
            });
        });
    });
    return ret;
}

IFunction* BuildFFI(void* symbol, EKind retKind, size_t retSize, const std::vector<EKind>& kinds, const std::vector<size_t>& sizes) {
    if (kinds.size() == 0) {
        return Builder0(symbol, retKind);
    } else if (kinds.size() == 1) {
        return Builder1(symbol, retKind, kinds[0]);
    } else if (kinds.size() == 2) {
        return Builder2(symbol, retKind, kinds[0], kinds[1]);
    }
    return nullptr;
}

} // namespace NFFI
} // namespace NIR
} // namespace NQumir