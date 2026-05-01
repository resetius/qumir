#pragma once

#include <cstdint>

namespace NQumir {
namespace NRuntime {

extern "C" {

// Color constants (ARGB, alpha in bits 31-24)
int64_t painter_transparent();
int64_t painter_white();
int64_t painter_black();
int64_t painter_gray();
int64_t painter_purple();
int64_t painter_blue();
int64_t painter_cyan();
int64_t painter_green();
int64_t painter_yellow();
int64_t painter_orange();
int64_t painter_red();

// Color construction (components in [0,255] for RGB/A; [0,360]/[0,100] for HSL/HSV/CMYK)
int64_t painter_rgb(int64_t r, int64_t g, int64_t b);
int64_t painter_rgba(int64_t r, int64_t g, int64_t b, int64_t a);
int64_t painter_cmyk(int64_t c, int64_t m, int64_t y, int64_t k);
int64_t painter_cmyka(int64_t c, int64_t m, int64_t y, int64_t k, int64_t a);
int64_t painter_hsl(int64_t h, int64_t s, int64_t l);
int64_t painter_hsla(int64_t h, int64_t s, int64_t l, int64_t a);
int64_t painter_hsv(int64_t h, int64_t s, int64_t v);
int64_t painter_hsva(int64_t h, int64_t s, int64_t v, int64_t a);

// Color decomposition
void painter_decompose_rgb(int64_t color, int64_t* r, int64_t* g, int64_t* b);
void painter_decompose_cmyk(int64_t color, int64_t* c, int64_t* m, int64_t* y, int64_t* k);
void painter_decompose_hsl(int64_t color, int64_t* h, int64_t* s, int64_t* l);
void painter_decompose_hsv(int64_t color, int64_t* h, int64_t* s, int64_t* v);

// Sheet info
int64_t painter_sheet_height();
int64_t painter_sheet_width();
int64_t painter_center_x();
int64_t painter_center_y();
int64_t painter_text_width(const char* text);
int64_t painter_get_pixel(int64_t x, int64_t y);

// Drawing parameters
void painter_pen(int64_t width, int64_t color);
void painter_brush(int64_t color);
void painter_no_brush();
void painter_density(int64_t d);
void painter_font(const char* family, int64_t size, bool bold, bool italic);

// Drawing commands
void painter_move_to(int64_t x, int64_t y);
void painter_line(int64_t x1, int64_t y1, int64_t x2, int64_t y2);
void painter_line_to(int64_t x, int64_t y);
void painter_polygon(int64_t n, int64_t* xs, int64_t* ys);
void painter_pixel(int64_t x, int64_t y, int64_t color);
void painter_rect(int64_t x, int64_t y, int64_t w, int64_t h);
void painter_ellipse(int64_t x, int64_t y, int64_t w, int64_t h);
void painter_circle(int64_t x, int64_t y, int64_t r);
void painter_text(int64_t x, int64_t y, const char* text);
void painter_fill(int64_t x, int64_t y);

// Sheet management
void painter_new_sheet(int64_t w, int64_t h, int64_t color);
void painter_load_sheet(const char* filename);
void painter_save_sheet(const char* filename);

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
