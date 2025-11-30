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
int32_t str_symbol_at(const char* s, int pos) ; // position in symbols, 1-indexed
char* str_from_lit(const char* s);
void str_retain(char* s);
void str_release(char* s);
char* str_concat(const char* a, const char* b);
int64_t str_compare(const char* a, const char* b);
int64_t str_len(const char* s);
int64_t str_unicode(const char* s); // unicode code point of first character
char* str_from_unicode(int64_t codepoint);
int64_t str_str(const char* needle, const char* haystack);
int64_t str_str_from(int64_t symbolStartPos, const char* needle, const char* haystack);
char* str_from_double(double x);
char* str_from_int(int64_t x);
void str_delete_symbols(char** s, int64_t startSymbol, int64_t count);
void str_insert_symbols(const char* insertStr, char** s, int64_t insertSymbolPos);
double str_to_double(const char* s, int8_t* outOk);
int64_t str_to_int(const char* s, int8_t* outOk);
char* str_input();

char* str_replace_sym(char* str, int32_t newSym, int64_t symIdx);

char* assign_from_lit(char* dest, const char* src);
char* assign_from_str(char* dest, char* src, int borrowed);

};

} // namespace NRuntime
} // namespace NQumir