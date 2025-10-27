#include "array.h"

#include <new>

namespace NQumir::NRuntime {

void* array_create(size_t sizeInBytes, size_t alignment) {
    return operator new(sizeInBytes, std::align_val_t(alignment));
}

void array_destroy(void* ptr, size_t alignment) {
    operator delete(ptr, std::align_val_t(alignment));
}

} // namespace NQumir::NRuntime
