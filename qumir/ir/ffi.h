#pragma once

#include "type.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace NQumir {
namespace NIR {

namespace NFFI {

// x86-64 SysV register classification of a by-value struct (Memory = passed by
// pointer / returned via sret). It selects a fake struct with the same field
// kinds, which the ABI classifies identically.
enum class EStructKind : uint8_t {
    None,
    Memory,
    Int,
    Sse,
    IntInt,
    IntSse,
    SseInt,
    SseSse,
};

template<class T>
T LoadArg(uint64_t x) {
    if constexpr (std::is_class_v<T>) {
        // A by-value struct argument arrives as a pointer to its storage.
        return *reinterpret_cast<const T*>(static_cast<uintptr_t>(x));
    } else {
        T ret;
        memcpy(&ret, &x, std::min(sizeof(T), sizeof(uint64_t)));
        return ret;
    }
}

template<typename T>
uint64_t StoreRet(T v, void* output = nullptr, size_t size = 0) {
    if (output != nullptr) {
        memcpy(output, &v, size);
        return 0;
    }

    uint64_t ret = 0;
    memcpy(&ret, &v, std::min(sizeof(T), sizeof(uint64_t)));
    return ret;
}

struct IFunction {
    virtual ~IFunction() = default;
    virtual uint64_t operator() (const uint64_t* args, size_t argCount) = 0;
};

// Builds a thunk for the VM convention: structs arrive by pointer but the
// native call takes register-class structs by value and Memory-class structs by
// pointer; a register-class struct return is materialized through args[0].
// Memory-class struct returns are not supported.
IFunction* BuildFFI(
    void* symbol,
    EKind retKind,
    EStructKind retStruct,
    size_t retSize,
    const std::vector<EKind>& argKinds,
    const std::vector<EStructKind>& argStructs) noexcept;

} // namespace NFFI
} // namespace NIR
} // namespace NQumir
