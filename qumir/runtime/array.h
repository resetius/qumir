#pragma once

#include <cstddef>

namespace NQumir::NRuntime {

extern "C" {

void* array_create(size_t sizeInBytes, size_t alignment);
void array_destroy(void* ptr);

} // extern "C"

} // namespace NQumir::NRuntime
