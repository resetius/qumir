#include "painter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace NQumir {
namespace NRuntime {

namespace {

// ARGB packing: bits 31-24 = alpha, 23-16 = red, 15-8 = green, 7-0 = blue
constexpr int64_t PackARGB(int64_t a, int64_t r, int64_t g, int64_t b) {
    return ((a & 0xFF) << 24) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

constexpr int64_t PackRGB(int64_t r, int64_t g, int64_t b) {
    return PackARGB(255, r, g, b);
}

// HSL/HSV helper: hue in [0,360], s/l/v in [0,100], returns channel in [0,255]
double HueToRGB(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0/6) return p + (q - p) * 6 * t;
    if (t < 1.0/2) return q;
    if (t < 2.0/3) return p + (q - p) * (2.0/3 - t) * 6;
    return p;
}

struct RGB { int64_t r, g, b; };

RGB HSLtoRGB(int64_t h, int64_t s, int64_t l) {
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

RGB HSVtoRGB(int64_t h, int64_t s, int64_t v) {
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

RGB CMYKtoRGB(int64_t c, int64_t m, int64_t y, int64_t k) {
    double cf = c / 100.0, mf = m / 100.0, yf = y / 100.0, kf = k / 100.0;
    return {
        static_cast<int64_t>(std::round((1 - cf) * (1 - kf) * 255)),
        static_cast<int64_t>(std::round((1 - mf) * (1 - kf) * 255)),
        static_cast<int64_t>(std::round((1 - yf) * (1 - kf) * 255)),
    };
}

struct PainterState {
    int64_t sheetWidth  = 800;
    int64_t sheetHeight = 600;
    int64_t penWidth    = 1;
    int64_t penColor    = PackRGB(0, 0, 0);
    int64_t brushColor  = PackRGB(255, 255, 255);
    bool    hasBrush    = true;
    int64_t density     = 100;
    std::string fontFamily = "Arial";
    int64_t fontSize    = 12;
    bool    fontBold    = false;
    bool    fontItalic  = false;
    int64_t curX        = 0;
    int64_t curY        = 0;
    std::vector<uint32_t> pixels; // sheetWidth * sheetHeight, ARGB packed
};

PainterState g_state;

} // namespace

extern "C" {

int64_t painter_transparent() { return PackARGB(0, 0, 0, 0); }
int64_t painter_white()       { return PackRGB(255, 255, 255); }
int64_t painter_black()       { return PackRGB(0, 0, 0); }
int64_t painter_gray()        { return PackRGB(128, 128, 128); }
int64_t painter_purple()      { return PackRGB(128, 0, 128); }
int64_t painter_blue()        { return PackRGB(0, 0, 255); }
int64_t painter_cyan()        { return PackRGB(0, 255, 255); }
int64_t painter_green()       { return PackRGB(0, 128, 0); }
int64_t painter_yellow()      { return PackRGB(255, 255, 0); }
int64_t painter_orange()      { return PackRGB(255, 165, 0); }
int64_t painter_red()         { return PackRGB(255, 0, 0); }

int64_t painter_rgb(int64_t r, int64_t g, int64_t b) {
    return PackRGB(r, g, b);
}

int64_t painter_rgba(int64_t r, int64_t g, int64_t b, int64_t a) {
    return PackARGB(a, r, g, b);
}

int64_t painter_cmyk(int64_t c, int64_t m, int64_t y, int64_t k) {
    auto rgb = CMYKtoRGB(c, m, y, k);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t painter_cmyka(int64_t c, int64_t m, int64_t y, int64_t k, int64_t a) {
    auto rgb = CMYKtoRGB(c, m, y, k);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

int64_t painter_hsl(int64_t h, int64_t s, int64_t l) {
    auto rgb = HSLtoRGB(h, s, l);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t painter_hsla(int64_t h, int64_t s, int64_t l, int64_t a) {
    auto rgb = HSLtoRGB(h, s, l);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

int64_t painter_hsv(int64_t h, int64_t s, int64_t v) {
    auto rgb = HSVtoRGB(h, s, v);
    return PackRGB(rgb.r, rgb.g, rgb.b);
}

int64_t painter_hsva(int64_t h, int64_t s, int64_t v, int64_t a) {
    auto rgb = HSVtoRGB(h, s, v);
    return PackARGB(a, rgb.r, rgb.g, rgb.b);
}

void painter_decompose_rgb(int64_t color, int64_t* r, int64_t* g, int64_t* b) {
    *r = (color >> 16) & 0xFF;
    *g = (color >> 8)  & 0xFF;
    *b =  color        & 0xFF;
}

void painter_decompose_cmyk(int64_t color, int64_t* c, int64_t* m, int64_t* y, int64_t* k) {
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

void painter_decompose_hsl(int64_t color, int64_t* h, int64_t* s, int64_t* l) {
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

void painter_decompose_hsv(int64_t color, int64_t* h, int64_t* s, int64_t* v) {
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

int64_t painter_sheet_height() { return g_state.sheetHeight; }
int64_t painter_sheet_width()  { return g_state.sheetWidth; }
int64_t painter_center_x()     { return g_state.sheetWidth  / 2; }
int64_t painter_center_y()     { return g_state.sheetHeight / 2; }

int64_t painter_text_width(const char* text) {
    // stub: approximate 7 pixels per character
    return static_cast<int64_t>(std::strlen(text)) * 7;
}

int64_t painter_get_pixel(int64_t x, int64_t y) {
    if (x < 0 || y < 0 || x >= g_state.sheetWidth || y >= g_state.sheetHeight) {
        return 0;
    }
    if (g_state.pixels.empty()) return 0;
    return static_cast<int64_t>(g_state.pixels[y * g_state.sheetWidth + x]);
}

void painter_pen(int64_t width, int64_t color) {
    std::cerr << "painter_pen width=" << width << " color=" << std::hex << color << std::dec << "\n";
    g_state.penWidth = width;
    g_state.penColor = color;
}

void painter_brush(int64_t color) {
    std::cerr << "painter_brush color=" << std::hex << color << std::dec << "\n";
    g_state.brushColor = color;
    g_state.hasBrush   = true;
}

void painter_no_brush() {
    std::cerr << "painter_no_brush\n";
    g_state.hasBrush = false;
}

void painter_density(int64_t d) {
    std::cerr << "painter_density " << d << "\n";
    g_state.density = d;
}

void painter_font(const char* family, int64_t size, bool bold, bool italic) {
    std::cerr << "painter_font family=" << family << " size=" << size
              << " bold=" << bold << " italic=" << italic << "\n";
    g_state.fontFamily = family;
    g_state.fontSize   = size;
    g_state.fontBold   = bold;
    g_state.fontItalic = italic;
}

void painter_move_to(int64_t x, int64_t y) {
    std::cerr << "painter_move_to (" << x << "," << y << ")\n";
    g_state.curX = x;
    g_state.curY = y;
}

void painter_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2) {
    std::cerr << "painter_line (" << x1 << "," << y1 << ") -> (" << x2 << "," << y2 << ")\n";
}

void painter_line_to(int64_t x, int64_t y) {
    std::cerr << "painter_line_to (" << g_state.curX << "," << g_state.curY
              << ") -> (" << x << "," << y << ")\n";
    g_state.curX = x;
    g_state.curY = y;
}

void painter_polygon(int64_t n, int64_t* xs, int64_t* ys) {
    std::cerr << "painter_polygon n=" << n << "\n";
}

void painter_pixel(int64_t x, int64_t y, int64_t color) {
    std::cerr << "painter_pixel (" << x << "," << y << ") color=" << std::hex << color << std::dec << "\n";
    if (x >= 0 && y >= 0 && x < g_state.sheetWidth && y < g_state.sheetHeight && !g_state.pixels.empty()) {
        g_state.pixels[y * g_state.sheetWidth + x] = static_cast<uint32_t>(color);
    }
}

void painter_rect(int64_t x, int64_t y, int64_t w, int64_t h) {
    std::cerr << "painter_rect (" << x << "," << y << ") " << w << "x" << h << "\n";
}

void painter_ellipse(int64_t x, int64_t y, int64_t w, int64_t h) {
    std::cerr << "painter_ellipse (" << x << "," << y << ") " << w << "x" << h << "\n";
}

void painter_circle(int64_t x, int64_t y, int64_t r) {
    std::cerr << "painter_circle center=(" << x << "," << y << ") r=" << r << "\n";
}

void painter_text(int64_t x, int64_t y, const char* text) {
    std::cerr << "painter_text (" << x << "," << y << ") \"" << text << "\"\n";
}

void painter_fill(int64_t x, int64_t y) {
    std::cerr << "painter_fill (" << x << "," << y << ")\n";
}

void painter_new_sheet(int64_t w, int64_t h, int64_t color) {
    std::cerr << "painter_new_sheet " << w << "x" << h << " color=" << std::hex << color << std::dec << "\n";
    if (w <= 0 || h <= 0 || w > 32767 || h > 32767) {
        throw std::runtime_error("Invalid sheet dimensions");
    }
    g_state.sheetWidth  = w;
    g_state.sheetHeight = h;
    g_state.pixels.assign(static_cast<size_t>(w * h), static_cast<uint32_t>(color));
}

void painter_load_sheet(const char* filename) {
    std::cerr << "painter_load_sheet \"" << filename << "\"\n";
}

void painter_save_sheet(const char* filename) {
    std::cerr << "painter_save_sheet \"" << filename << "\"\n";
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
