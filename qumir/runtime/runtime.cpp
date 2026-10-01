#include "runtime.h"

#include <stdexcept>
#include <setjmp.h>
#include <cstdio>

static thread_local jmp_buf* tls_jmp_buf = nullptr;
static thread_local char tls_error_buf[4096];
static thread_local int64_t tls_locator = 0;

void __record_locator(int64_t line) { tls_locator = line; }
int64_t __current_locator(void) { return tls_locator; }

void __set_jmp_target(jmp_buf* buf) { tls_jmp_buf = buf; }
void __clear_jmp_target(void) { tls_jmp_buf = nullptr; }
const char* __get_runtime_error(void) { return tls_error_buf; }

void __ensure(bool condition, const char* message) {
    if (!condition) {
        if (tls_jmp_buf) {
            snprintf(tls_error_buf, sizeof(tls_error_buf), "Runtime assertion failed: %s", message);
            longjmp(*tls_jmp_buf, 1);
        } else {
            throw std::runtime_error(std::string("Runtime assertion failed: ") + message);
        }
    }
}
