#pragma once

#include <stdint.h>

namespace NQumir {
namespace NRuntime {

struct TString {
    int64_t Rc;
    int64_t Length;
    char Data[0];
};

extern "C" {

char* str_from_lit(const char* s);
void str_retain(char* s);
void str_release(char* s);
char* str_concat(const char* a, const char* b);
int64_t str_compare(const char* a, const char* b);

};

} // namespace NRuntime
} // namespace NQumir