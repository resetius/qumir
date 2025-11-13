#pragma once

namespace NQumir {
namespace NRuntime {

extern "C" {

void turtle_pen_up();
void turtle_pen_down();
void turtle_forward(double distance);
void turtle_backward(double distance);
void turtle_turn_left(double angle);
void turtle_turn_right(double angle);
void turtle_save_state();
void turtle_restore_state();

} // extern "C"

} // namespace NRuntime
} // namespace NQumir