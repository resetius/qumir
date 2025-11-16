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
    // strings are 1-indexed
    if (!s) {
        return nullptr;
    }

    TString* str = (TString*)(s - offsetof(TString, Data));
    if (!str->Utf8Indices) {
        build_utf8_indices(str);
    }
    if (startSymbol < 1) {
        startSymbol = 1;
    }
    if (endSymbol >= str->Symbols) {
        endSymbol = str->Symbols;
    }

    if (startSymbol > endSymbol) {
        return nullptr;
    }

    return str_from_lit_(s + str->Utf8Indices[startSymbol-1], str->Utf8Indices[endSymbol] - str->Utf8Indices[startSymbol-1]);
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

int64_t str_len(const char* s) {
    if (!s) return 0;
    TString* str = (TString*)(s - offsetof(TString, Data));
    if (!str->Utf8Indices) {
        build_utf8_indices(str);
    }
    return str->Symbols;
}

int64_t str_unicode(const char* s) {
    if (!s) return -1;
    unsigned char c = static_cast<unsigned char>(s[0]);
    if (c < 0x80) {
        return c;
    } else if ((c & 0b11100000) == 0b11000000) {
        // 2-byte sequence
        unsigned char c2 = static_cast<unsigned char>(s[1]);
        return ((c & 0b00011111) << 6) | (c2 & 0b00111111);
    } else if ((c & 0b11110000) == 0b11100000) {
        // 3-byte sequence
        unsigned char c2 = static_cast<unsigned char>(s[1]);
        unsigned char c3 = static_cast<unsigned char>(s[2]);
        return ((c & 0b00001111) << 12) | ((c2 & 0b00111111) << 6) | (c3 & 0b00111111);
    } else if ((c & 0b11111000) == 0b11110000) {
        // 4-byte sequence
        unsigned char c2 = static_cast<unsigned char>(s[1]);
        unsigned char c3 = static_cast<unsigned char>(s[2]);
        unsigned char c4 = static_cast<unsigned char>(s[3]);
        return ((c & 0b00000111) << 18) | ((c2 & 0b00111111) << 12) | ((c3 & 0b00111111) << 6) | (c4 & 0b00111111);
    }
    return -1; // invalid UTF-8
}

char* str_from_unicode(int64_t codepoint) {
    char buffer[5] = {0};
    if (codepoint < 0x80) {
        buffer[0] = static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        buffer[0] = static_cast<char>(0b11000000 | ((codepoint >> 6) & 0b00011111));
        buffer[1] = static_cast<char>(0b10000000 | (codepoint & 0b00111111));
    } else if (codepoint < 0x10000) {
        buffer[0] = static_cast<char>(0b11100000 | ((codepoint >> 12) & 0b00001111));
        buffer[1] = static_cast<char>(0b10000000 | ((codepoint >> 6) & 0b00111111));
        buffer[2] = static_cast<char>(0b10000000 | (codepoint & 0b00111111));
    } else if (codepoint <= 0x10FFFF) {
        buffer[0] = static_cast<char>(0b11110000 | ((codepoint >> 18) & 0b00000111));
        buffer[1] = static_cast<char>(0b10000000 | ((codepoint >> 12) & 0b00111111));
        buffer[2] = static_cast<char>(0b10000000 | ((codepoint >> 6) & 0b00111111));
        buffer[3] = static_cast<char>(0b10000000 | (codepoint & 0b00111111));
    } else {
        return nullptr; // invalid codepoint
    }
    return str_from_lit_(buffer, std::strlen(buffer));
}

char* assign_from_lit(char* dest, const char* src) {
    if (!src) {
        str_release(dest);
        return dest;
    }
    if (!dest) {
        return str_from_lit(src);
    }
    auto srcLen = std::strlen(src);
    TString* destStr = (TString*)(dest - offsetof(TString, Data));
    if (destStr->Length >= (int)srcLen) {
        // can fit in existing allocation
        std::memcpy(dest, src, destStr->Length + 1);
        destStr->Length = srcLen;
        return dest;
    } else {
        // need to reallocate inplace
        int newSize = sizeof(TString) + srcLen + 1;
        TString* newStr = (TString*)realloc(destStr, newSize);
        newStr->Length = srcLen;
        std::memcpy(newStr->Data, src, newStr->Length + 1);
        return newStr->Data;
    }
}

char* assign_from_str(char* dest, char* src, int borrowed) {
    if (borrowed) {
        str_retain(src);
    }
    str_release(dest);
    return src;
}

} // namespace NRuntime
} // namespace NQumir