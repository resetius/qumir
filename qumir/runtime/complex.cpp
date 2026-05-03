#include "complex.h"

#include <cmath>

namespace NQumir {
namespace NRuntime {

extern "C" {

double complex_re(const komplex* a)  { return a->re; }
double complex_im(const komplex* a)  { return a->im; }
double complex_abs(const komplex* a) { return std::sqrt(a->re * a->re + a->im * a->im); }
double complex_arg(const komplex* a) { return std::atan2(a->im, a->re); }

void complex_i(komplex* r) {
    r->re = 0.0;
    r->im = 1.0;
}

void complex_conj(komplex* r, const komplex* a) {
    r->re =  a->re;
    r->im = -a->im;
}

void complex_add(komplex* r, const komplex* a, const komplex* b) {
    r->re = a->re + b->re;
    r->im = a->im + b->im;
}

void complex_sub(komplex* r, const komplex* a, const komplex* b) {
    r->re = a->re - b->re;
    r->im = a->im - b->im;
}

void complex_mul(komplex* r, const komplex* a, const komplex* b) {
    r->re = a->re * b->re - a->im * b->im;
    r->im = a->re * b->im + a->im * b->re;
}

void complex_div(komplex* r, const komplex* a, const komplex* b) {
    double denom = b->re * b->re + b->im * b->im;
    r->re = (a->re * b->re + a->im * b->im) / denom;
    r->im = (a->im * b->re - a->re * b->im) / denom;
}

void complex_neg(komplex* r, const komplex* a) {
    r->re = -a->re;
    r->im = -a->im;
}

int64_t complex_eq(const komplex* a, const komplex* b) {
    return (a->re == b->re && a->im == b->im) ? 1 : 0;
}

int64_t complex_ne(const komplex* a, const komplex* b) {
    return (a->re != b->re || a->im != b->im) ? 1 : 0;
}

void complex_from_float(komplex* r, double x) {
    r->re = x;
    r->im = 0.0;
}

void complex_from_int(komplex* r, int64_t n) {
    r->re = static_cast<double>(n);
    r->im = 0.0;
}

double  complex_to_float(const komplex* a) { return a->re; }
int64_t complex_to_int(const komplex* a)   { return static_cast<int64_t>(a->re); }

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
