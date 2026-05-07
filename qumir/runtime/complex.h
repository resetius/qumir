#pragma once

#include <cstdint>

namespace NQumir {
namespace NRuntime {

// Memory layout of компл: two consecutive doubles
struct komplex {
    double re;
    double im;
};

extern "C" {

// ── Scalar accessors (return by value) ───────────────────────────────────────
double  complex_re(komplex a);
double  complex_im(komplex a);
double  complex_abs(komplex a);
double  complex_arg(komplex a);

// ── Struct-returning functions ───────────────────────────────────────────────
komplex complex_i();
komplex complex_conj(komplex a);
komplex complex_add(komplex a, komplex b);
komplex complex_sub(komplex a, komplex b);
komplex complex_mul(komplex a, komplex b);
komplex complex_div(komplex a, komplex b);
komplex complex_neg(komplex a);

// ── Comparison (return bool as int64) ────────────────────────────────────────
int64_t complex_eq(komplex a, komplex b);
int64_t complex_ne(komplex a, komplex b);

// ── Casts ─────────────────────────────────────────────────────────────────────
komplex complex_from_float(double x);
komplex complex_from_int(int64_t n);
komplex complex_from_imag(double im); // 2i syntax: компл(0, im)
double  complex_to_float(komplex a);  // returns Re
int64_t complex_to_int(komplex a);    // returns (int64_t)Re

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
