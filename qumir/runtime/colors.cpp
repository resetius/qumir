#include "colors.h"
#include "io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>

namespace NQumir {
namespace NRuntime {

extern "C" {

int64_t color_transparent() { return PackARGB(0, 0, 0, 0); }
int64_t color_white()       { return PackRGB(255, 255, 255); }
int64_t color_black()       { return PackRGB(0, 0, 0); }
int64_t color_gray()        { return PackRGB(128, 128, 128); }
int64_t color_purple()      { return PackRGB(128, 0, 128); }
int64_t color_blue()        { return PackRGB(0, 0, 255); }
int64_t color_cyan()        { return PackRGB(0, 255, 255); }
int64_t color_green()       { return PackRGB(0, 128, 0); }
int64_t color_yellow()      { return PackRGB(255, 255, 0); }
int64_t color_orange()      { return PackRGB(255, 165, 0); }
int64_t color_red()         { return PackRGB(255, 0, 0); }

int64_t color_rgb(int64_t r, int64_t g, int64_t b) {
    return PackRGB(r, g, b);
}

int64_t color_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    return PackARGB(a, r, g, b);
}

int64_t color_cmyk(int64_t c, int64_t m, int64_t y, int64_t k) {
    auto rgb = CMYKtoRGB(c, m, y, k);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t color_cmyka(int64_t c, int64_t m, int64_t y, int64_t k, int64_t a) {
    auto rgb = CMYKtoRGB(c, m, y, k);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

int64_t color_hsl(int64_t h, int64_t s, int64_t l) {
    auto rgb = HSLtoRGB(h, s, l);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t color_hsla(int64_t h, int64_t s, int64_t l, int64_t a) {
    auto rgb = HSLtoRGB(h, s, l);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

int64_t color_hsv(int64_t h, int64_t s, int64_t v) {
    auto rgb = HSVtoRGB(h, s, v);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t color_hsva(int64_t h, int64_t s, int64_t v, int64_t a) {
    auto rgb = HSVtoRGB(h, s, v);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

void color_decompose_rgb(int64_t color, int64_t* r, int64_t* g, int64_t* b) {
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8)  & 0xFF;
    *b =  color        & 0xFF;
}

void color_decompose_cmyk(int64_t color, int64_t* c, int64_t* m, int64_t* y, int64_t* k) {
    int64_t r = (color >> 16) & 0xFF;
    int64_t g = (color >> 8)  & 0xFF;
    int64_t b =  color        & 0xFF;
    double rf = r / 255.0, gf = g / 255.0, bf = b / 255.0;
    double kf = 1.0 - std::max({rf, gf, bf});
    if (kf >= 1.0) {
        *c = 0; *m = 0; *y = 0; *k = 100;
    } else {
        *c = static_cast<int64_t>(std::round((1 - rf - kf) / (1 - kf) * 100));
        *m = static_cast<int64_t>(std::round((1 - gf - kf) / (1 - kf) * 100));
        *y = static_cast<int64_t>(std::round((1 - bf - kf) / (1 - kf) * 100));
        *k = static_cast<int64_t>(std::round(kf * 100));
    }
}

void color_decompose_hsl(int64_t color, int64_t* h, int64_t* s, int64_t* l) {
    double r = ((color >> 16) & 0xFF) / 255.0;
    double g = ((color >> 8)  & 0xFF) / 255.0;
    double b = ( color        & 0xFF) / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double lf = (mx + mn) / 2.0;
    double sf = 0, hf = 0;
    if (mx != mn) {
        double d = mx - mn;
        sf = lf > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r)      hf = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) hf = (b - r) / d + 2;
        else              hf = (r - g) / d + 4;
        hf /= 6;
    }
    *h = static_cast<int64_t>(std::round(hf * 360));
    *s = static_cast<int64_t>(std::round(sf * 100));
    *l = static_cast<int64_t>(std::round(lf * 100));
}

void color_decompose_hsv(int64_t color, int64_t* h, int64_t* s, int64_t* v) {
    double r = ((color >> 16) & 0xFF) / 255.0;
    double g = ((color >> 8)  & 0xFF) / 255.0;
    double b = ( color        & 0xFF) / 255.0;
    double mx = std::max({r, g, b}), mn = std::min({r, g, b});
    double d = mx - mn;
    double hf = 0, sf = (mx == 0) ? 0 : d / mx;
    if (d != 0) {
        if (mx == r)      hf = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) hf = (b - r) / d + 2;
        else              hf = (r - g) / d + 4;
        hf /= 6;
    }
    *h = static_cast<int64_t>(std::round(hf * 360));
    *s = static_cast<int64_t>(std::round(sf * 100));
    *v = static_cast<int64_t>(std::round(mx * 100));
}

void color_print(int64_t color) {
    const uint32_t c = static_cast<uint32_t>(color);
    const int r = (c >> 16) & 0xFF;
    const int g = (c >>  8) & 0xFF;
    const int b =  c        & 0xFF;
    std::ostringstream oss;
    oss << '#' << std::hex << std::uppercase << std::setfill('0')
        << std::setw(2) << r << std::setw(2) << g << std::setw(2) << b;
    *GetOutputStream() << oss.str();
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir