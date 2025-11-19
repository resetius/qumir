#include "runtime.h"

#include <stdexcept>

void __ensure(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(std::string("Runtime assertion failed: ") + message);
    }
}
