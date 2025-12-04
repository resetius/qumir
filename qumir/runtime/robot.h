#pragma once

#include <cstdint>

namespace NQumir {
namespace NRuntime {

extern "C" {

void robot_left();
void robot_right();
void robot_up();
void robot_down();
void robot_paint();

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