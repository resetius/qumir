#include "string.h"

#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <algorithm>
#include <iostream>

namespace NQumir {
namespace NRuntime {

char* str_from_lit_(const char* s, int len) {
    TString* str = (TString*)calloc(1, sizeof(TString) + len + 1);
    str->Rc = 1;
    str->Length = len;
    std::memcpy(str->Data, s, len);
    return str->Data;
}

char* str_from_lit(const char* s) {
    //std::cerr << "from_lit '" << s << "'\n";
    if (!s) {
        return nullptr;
    }
    int len = std::strlen(s);
    return str_from_lit_(s, len);
}

void build_utf8_indices(TString* str) {
    if (!str) return;
    str->Utf8Indices = (int*)malloc(sizeof(int) * (str->Length + 1));
    int index = 0;
    for (int i = 0; i < str->Length; ++i) {
        if ((str->Data[i] & 0b11000000) != 0b10000000) {
            str->Utf8Indices[index++] = i;
        }
    }
    str->Symbols = index;
}

char* str_slice(const char* s, int startSymbol, int endSymbol) {
    if (!s) {
        return nullptr;
    }

    TString* str = (TString*)(s - offsetof(TString, Data));
    if (!str->Utf8Indices) {
        build_utf8_indices(str);
    }
    if (startSymbol < 0) {
        startSymbol = 0;
    }
    if (endSymbol >= str->Symbols-1) {
        endSymbol = str->Symbols-1;
    }

    if (startSymbol > endSymbol) {
        return nullptr;
    }

    return str_from_lit_(s + str->Utf8Indices[startSymbol], str->Utf8Indices[endSymbol + 1] - str->Utf8Indices[startSymbol]);
}

void str_retain(char* s) {
    if (!s) return;
    TString* str = (TString*)(s - offsetof(TString, Data));
    str->Rc += 1;
}

void str_release(char* s) {
    //std::cerr << "release '" <<  (s ? s : "(null)")<< "' " << (void*)s << "\n";
    if (!s) return;
    TString* str = (TString*)(s - offsetof(TString, Data));
    if (--str->Rc == 0) {
        free(str->Utf8Indices);
        free(str);
    }
}

char* str_concat(const char* a, const char* b) {
    //std::cerr << "concat '" << a << "' + '" << b << "'\n";
    if (!a) { a = ""; }
    if (!b) { b = ""; }
    int lenA = strlen(a);
    int lenB = strlen(b);
    TString* strC = (TString*)calloc(1, sizeof(TString) + lenA + lenB + 1);
    strC->Rc = 1;
    strC->Length = lenA + lenB;
    std::memcpy(strC->Data, a, lenA);
    std::memcpy(strC->Data + lenA, b, lenB + 1);
    return strC->Data;
}

int64_t str_compare(const char* a, const char* b) {
    if (a == b) return 0;
    TString* strA = (TString*)(a - offsetof(TString, Data));
    TString* strB = (TString*)(b - offsetof(TString, Data));
    int lenA = strA->Length;
    int lenB = strB->Length;
    int minLen = std::min(lenA, lenB);
    int cmp = std::memcmp(a, b, minLen);
    if (cmp != 0) {
        return (cmp < 0) ? -1 : 1;
    }
    return (lenA < lenB) ? -1 : (lenA > lenB) ? 1 : 0;
}

} // namespace NRuntime
} // namespace NQumir