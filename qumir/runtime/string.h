#pragma once

#include <stdint.h>

namespace NQumir {
namespace NRuntime {

struct TString {
    int* Utf8Indices;
    int64_t Symbols;
    int64_t Rc;
    int64_t Length;
    char Data[0];
};

extern "C" {

char* str_slice(const char* s, int startSymbol, int endSymbol);
char* str_from_lit(const char* s);
void str_retain(char* s);
void str_release(char* s);
char* str_concat(const char* a, const char* b);
int64_t str_compare(const char* a, const char* b);
int64_t str_len(const char* s);
int64_t str_unicode(const char* s); // unicode code point of first character
char* str_from_unicode(int64_t codepoint);
int64_t str_str(const char* haystack, const char* needle);

char* assign_from_lit(char* dest, const char* src);
char* assign_from_str(char* dest, char* src, int borrowed);

};

} // namespace NRuntime
} // namespace NQumir