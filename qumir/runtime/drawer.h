#pragma once

#include <cstdint>

namespace NQumir {
namespace NRuntime {

extern "C" {

void drawer_pen_up();
void drawer_pen_down();
void drawer_set_color(int64_t color);
void drawer_move_to(double x, double y);
void drawer_move_by(double dx, double dy);
void drawer_write_text(double width, const char* text);

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
