#include "io.h"

namespace NQumir {
namespace NRuntime {

namespace {

std::istream *In = &std::cin;
std::ostream *Out = &std::cout;

};

void SetOutputStream(std::ostream* os) {
    Out = os ? os : &std::cout;
}

void SetInputStream(std::istream* is) {
    In = is ? is : &std::cin;
}

extern "C" {

double input_double() {
    double x;
    (*In) >> x;
    return x;
}

int64_t input_int64() {
    int64_t x;
    (*In) >> x;
    return x;
}

void output_double(double x) {
    (*Out) << x;
}

void output_int64(int64_t x) {
    (*Out) << x;
}

void output_string(const char* s) {
    if (!s) {return;}
    (*Out) << s;
}

void output_bool(int64_t b) {
    (*Out) << (b ? "да" : "нет");
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
