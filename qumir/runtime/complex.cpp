#include "complex.h"

#include <cmath>

namespace NQumir {
namespace NRuntime {

extern "C" {

double complex_re(komplex a)  { return a.re; }
double complex_im(komplex a)  { return a.im; }
double complex_abs(komplex a) { return std::sqrt(a.re * a.re + a.im * a.im); }
double complex_arg(komplex a) { return std::atan2(a.im, a.re); }

komplex complex_i() {
    return {0.0, 1.0};
}

komplex complex_conj(komplex a) {
    return {a.re, -a.im};
}

komplex complex_add(komplex a, komplex b) {
    return {a.re + b.re, a.im + b.im};
}

komplex complex_sub(komplex a, komplex b) {
    return {a.re - b.re, a.im - b.im};
}

komplex complex_mul(komplex a, komplex b) {
    return {a.re * b.re - a.im * b.im, a.re * b.im + a.im * b.re};
}

komplex complex_div(komplex a, komplex b) {
    double denom = b.re * b.re + b.im * b.im;
    return {(a.re * b.re + a.im * b.im) / denom, (a.im * b.re - a.re * b.im) / denom};
}

komplex complex_neg(komplex a) {
    return {-a.re, -a.im};
}

int64_t complex_eq(komplex a, komplex b) {
    return (a.re == b.re && a.im == b.im) ? 1 : 0;
}

int64_t complex_ne(komplex a, komplex b) {
    return (a.re != b.re || a.im != b.im) ? 1 : 0;
}

komplex complex_from_float(double x) {
    return {x, 0.0};
}

komplex complex_from_int(int64_t n) {
    return {static_cast<double>(n), 0.0};
}

double  complex_to_float(komplex a) { return a.re; }
int64_t complex_to_int(komplex a)   { return static_cast<int64_t>(a.re); }

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
