#pragma once

#include "type.h"

#include <stdexcept>
#include <tuple>

#include <iostream>

namespace NQumir {
namespace NIR {

namespace NFFI {

template<class T>
T LoadArg(uint64_t x) {
    T ret;
    memcpy(&ret, &x, std::min(sizeof(T), sizeof(uint64_t)));
    return ret;
}

template<typename T>
uint64_t StoreRet(T v) {
    uint64_t ret;
    memcpy(&ret, &v, std::min(sizeof(T), sizeof(uint64_t)));
    return ret;
}

struct IFunction {
    virtual ~IFunction() = default;
    virtual uint64_t operator() (const uint64_t* args, size_t argCount) = 0;
};

IFunction* BuildFFI(void* symbol, EKind retKind, size_t retSize, const std::vector<EKind>& kinds, const std::vector<size_t>& sizes);

} // namespace NFFI
} // namespace NIR
} // namespace NQumir
