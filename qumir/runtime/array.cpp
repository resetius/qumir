#include "array.h"

#include <new>
#include <iostream>

namespace NQumir::NRuntime {

void* array_create(size_t sizeInBytes) {
    auto ptr = operator new(sizeInBytes, std::align_val_t(8));
    return ptr;
}

void array_destroy(void* ptr) {
    operator delete(ptr, std::align_val_t(8));
}

} // namespace NQumir::NRuntime
