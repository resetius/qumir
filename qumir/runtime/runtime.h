#pragma once

#include "math.h"
#include "io.h"
#include "string.h"
#include "array.h"
#include "turtle.h"

#include <setjmp.h>

extern "C" {
    void __ensure(bool condition, const char* message);
    // Source location (as in TLocation) of the call about to be made to a
    // command marked NeedsLocator.
    void __record_locator(int64_t line, int64_t byte, int64_t column);
    // JIT error escape: set a longjmp target so __ensure can jump back to host code
    // instead of throwing through JIT frames (which lack unwind info on macOS).
    void __set_jmp_target(jmp_buf* buf);
    void __clear_jmp_target(void);
    const char* __get_runtime_error(void);
}