#pragma once

#include <cstdint>

namespace NQumir {
namespace NRuntime {

// ARGB packing: bits 31-24 = alpha, 23-16 = red, 15-8 = green, 7-0 = blue
inline constexpr int64_t PackARGB(int64_t a, int64_t r, int64_t g, int64_t b) {
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

inline constexpr int64_t PackRGB(int64_t r, int64_t g, int64_t b) {
    return PackARGB(255, r, g, b);
}

extern "C" {

// Color decomposition
void color_decompose_cmyk(int64_t color, int64_t* c, int64_t* m, int64_t* y, int64_t* k);
void color_decompose_hsl(int64_t color, int64_t* h, int64_t* s, int64_t* l);
void color_decompose_hsv(int64_t color, int64_t* h, int64_t* s, int64_t* v);

// Output: prints "#RRGGBB" to current output stream
void color_print(int64_t color);

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
