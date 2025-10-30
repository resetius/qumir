#include "array.h"

#include <new>
#include <iostream>
#include <cstring>

namespace NQumir::NRuntime {

void* array_create(size_t sizeInBytes) {
    auto ptr = operator new(sizeInBytes, std::align_val_t(8));
    memset(ptr, 0, sizeInBytes);
    return ptr;
}

void array_destroy(void* ptr) {
    operator delete(ptr, std::align_val_t(8));
}

} // namespace NQumir::NRuntime
