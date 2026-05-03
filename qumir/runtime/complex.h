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
double  complex_re(const komplex* a);
double  complex_im(const komplex* a);
double  complex_abs(const komplex* a);
double  complex_arg(const komplex* a);

// ── Struct-returning functions (C ABI: hidden first arg = out pointer) ────────
void complex_i(komplex* r);
void complex_conj(komplex* r, const komplex* a);
void complex_add(komplex* r, const komplex* a, const komplex* b);
void complex_sub(komplex* r, const komplex* a, const komplex* b);
void complex_mul(komplex* r, const komplex* a, const komplex* b);
void complex_div(komplex* r, const komplex* a, const komplex* b);
void complex_neg(komplex* r, const komplex* a);

// ── Comparison (return bool as int64) ────────────────────────────────────────
int64_t complex_eq(const komplex* a, const komplex* b);
int64_t complex_ne(const komplex* a, const komplex* b);

// ── Casts ─────────────────────────────────────────────────────────────────────
void    complex_from_float(komplex* r, double x);
void    complex_from_int(komplex* r, int64_t n);
double  complex_to_float(const komplex* a);  // returns Re
int64_t complex_to_int(const komplex* a);    // returns (int64_t)Re

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
