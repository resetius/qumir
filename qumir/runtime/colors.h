#pragma once

#include <cstdint>
#include <cmath>

namespace NQumir {
namespace NRuntime {

// ARGB packing: bits 31-24 = alpha, 23-16 = red, 15-8 = green, 7-0 = blue
inline constexpr int64_t PackARGB(int64_t a, int64_t r, int64_t g, int64_t b) {
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

inline constexpr int64_t PackRGB(int64_t r, int64_t g, int64_t b) {
    return PackARGB(255, r, g, b);
}

// HSL/HSV helper: hue in [0,360], s/l/v in [0,100], returns channel in [0,255]
inline double HueToRGB(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0/6) return p + (q - p) * 6 * t;
    if (t < 1.0/2) return q;
    if (t < 2.0/3) return p + (q - p) * (2.0/3 - t) * 6;
    return p;
}

struct RGB { int64_t r, g, b; };

inline RGB HSLtoRGB(int64_t h, int64_t s, int64_t l) {
    double hf = h / 360.0, sf = s / 100.0, lf = l / 100.0;
    if (sf == 0) {
        int64_t v = static_cast<int64_t>(std::round(lf * 255));
        return {v, v, v};
    }
    double q = lf < 0.5 ? lf * (1 + sf) : lf + sf - lf * sf;
    double p = 2 * lf - q;
    return {
        static_cast<int64_t>(std::round(HueToRGB(p, q, hf + 1.0/3) * 255)),
        static_cast<int64_t>(std::round(HueToRGB(p, q, hf)         * 255)),
        static_cast<int64_t>(std::round(HueToRGB(p, q, hf - 1.0/3) * 255)),
    };
}

inline RGB HSVtoRGB(int64_t h, int64_t s, int64_t v) {
    double hf = h / 60.0, sf = s / 100.0, vf = v / 100.0;
    int64_t i = static_cast<int64_t>(hf) % 6;
    double f = hf - std::floor(hf);
    double p = vf * (1 - sf);
    double q = vf * (1 - f * sf);
    double t = vf * (1 - (1 - f) * sf);
    double r, g, b;
    switch (i) {
        case 0: r = vf; g = t;  b = p;  break;
        case 1: r = q;  g = vf; b = p;  break;
        case 2: r = p;  g = vf; b = t;  break;
        case 3: r = p;  g = q;  b = vf; break;
        case 4: r = t;  g = p;  b = vf; break;
        default:r = vf; g = p;  b = q;  break;
    }
    return {
        static_cast<int64_t>(std::round(r * 255)),
        static_cast<int64_t>(std::round(g * 255)),
        static_cast<int64_t>(std::round(b * 255)),
    };
}

inline RGB CMYKtoRGB(int64_t c, int64_t m, int64_t y, int64_t k) {
    double cf = c / 100.0, mf = m / 100.0, yf = y / 100.0, kf = k / 100.0;
    return {
        static_cast<int64_t>(std::round((1 - cf) * (1 - kf) * 255)),
        static_cast<int64_t>(std::round((1 - mf) * (1 - kf) * 255)),
        static_cast<int64_t>(std::round((1 - yf) * (1 - kf) * 255)),
    };
}

extern "C" {

// Color construction (components in [0,255] for RGB/A; [0,360]/[0,100] for HSL/HSV/CMYK)
int64_t color_hsv(int64_t h, int64_t s, int64_t v);
int64_t color_hsva(int64_t h, int64_t s, int64_t v, int64_t a);

// Color decomposition
void color_decompose_cmyk(int64_t color, int64_t* c, int64_t* m, int64_t* y, int64_t* k);
void color_decompose_hsl(int64_t color, int64_t* h, int64_t* s, int64_t* l);
void color_decompose_hsv(int64_t color, int64_t* h, int64_t* s, int64_t* v);

// Output: prints "#RRGGBB" to current output stream
void color_print(int64_t color);

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
