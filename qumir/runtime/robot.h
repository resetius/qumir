#pragma once

#include <cstdint>
#include <cstddef>

namespace NQumir {
struct ITypeErasedFuture;

namespace NRuntime {

extern "C" {

ITypeErasedFuture* robot_left();
ITypeErasedFuture* robot_right();
ITypeErasedFuture* robot_up();
ITypeErasedFuture* robot_down();
ITypeErasedFuture* robot_paint();
size_t robot_process_events();

bool robot_left_free();
bool robot_right_free();
bool robot_top_free();
bool robot_bottom_free();

bool robot_left_wall();
bool robot_right_wall();
bool robot_top_wall();
bool robot_bottom_wall();

bool robot_cell_painted();
bool robot_cell_clean();

double robot_radiation();
double robot_temperature();

}

} // namespace NRuntime
} // namespace NQumir
