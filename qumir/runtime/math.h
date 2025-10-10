#pragma once

#include <stdint.h>

extern "C" {

double cotan(double x);
int64_t min_int64_t(int64_t a, int64_t b);
int64_t max_int64_t(int64_t a, int64_t b);
double min_double(double a, double b);
double max_double(double a, double b);
int sign(double x);
int64_t trunc_double(double x);
double rand_double(double x);
double rand_double_range(double a, double b);
uint64_t rand_uint64(uint64_t x);
uint64_t rand_uint64_range(uint64_t a, uint64_t b);
int64_t max_limit_int64_t();
int64_t div_qum(int64_t a, int64_t b);
int64_t mod_qum(int64_t a, int64_t b);
double max_limit_double();
double fpow(double a, int n);

} // extern "C"