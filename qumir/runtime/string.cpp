#include "string.h"

#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <algorithm>
#include <iostream>

namespace NQumir {
namespace NRuntime {

char* str_from_lit(const char* s) {
    //std::cerr << "from_lit '" << s << "'\n";
    int len = std::strlen(s);
    TString* str = (TString*)malloc(sizeof(TString) + len + 1);
    str->Rc = 1;
    str->Length = len;
    std::memcpy(str->Data, s, len + 1);
    return str->Data;
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
        free(str);
    }
}

char* str_concat(const char* a, const char* b) {
    //std::cerr << "concat '" << a << "' + '" << b << "'\n";
    if (!a) { a = ""; }
    if (!b) { b = ""; }
    int lenA = strlen(a);
    int lenB = strlen(b);
    TString* strC = (TString*)malloc(sizeof(TString) + lenA + lenB + 1);
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