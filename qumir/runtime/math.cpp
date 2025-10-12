#include "math.h"

#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <iostream>

#include <limits>

double cotan(double x) {
    return 1.0 / tan(x);
}

int64_t min_int64_t(int64_t a, int64_t b) {
    return a < b ? a : b;
}

int64_t max_int64_t(int64_t a, int64_t b) {
    return a > b ? a : b;
}

double min_double(double a, double b) {
    return a < b ? a : b;
}

double max_double(double a, double b) {
    return a > b ? a : b;
}

int sign(double x) {
    return (x > 0) - (x < 0);
}

int64_t trunc_double(double x) {
    return static_cast<int64_t>(x);
}

double rand_double(double x) {
    // random on [0,x]
    return static_cast<double>(rand()) / RAND_MAX * x;
}

double rand_double_range(double a, double b) {
    // random on [a,b]
    return a + static_cast<double>(rand()) / RAND_MAX * (b - a);
}

uint64_t rand_uint64(uint64_t x) {
    // random on [0,x]
    if (x == 0) return 0;
    return static_cast<uint64_t>(rand()) % x;
}

uint64_t rand_uint64_range(uint64_t a, uint64_t b) {
    // random on [a,b]
    if (b <= a) return a;
    return a + static_cast<uint64_t>(rand()) % (b - a);
}

int64_t div_qum(int64_t a, int64_t b) {
    if (b == 0) {
        // division by zero
        return 0;
    }
    return a / b;
}

int64_t mod_qum(int64_t a, int64_t b) {
    if (b == 0) {
        // division by zero
        return 0;
    }
    return a % b;
}

double fpow(double a, int n) {
    if (n < 0) {
        if (a == 0.0) {
            // division by zero
            return NAN;
        }
        return 1.0 / fpow(a, -n);
    }
    double result = 1.0;
    while (n) {
        if (n & 1) {
            result *= a;
        }
        a *= a;
        n >>= 1;
    }
    return result;
}
