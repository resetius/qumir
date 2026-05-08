#include "painter.h"
#include "colors.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace NQumir {
namespace NRuntime {

namespace {

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
