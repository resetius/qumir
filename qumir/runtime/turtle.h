#pragma once

#include <cstddef>

namespace NQumir {
struct ITypeErasedFuture;

namespace NRuntime {

extern "C" {

ITypeErasedFuture* turtle_pen_up();
ITypeErasedFuture* turtle_pen_down();
ITypeErasedFuture* turtle_forward(double distance);
ITypeErasedFuture* turtle_backward(double distance);
ITypeErasedFuture* turtle_turn_left(double angle);
ITypeErasedFuture* turtle_turn_right(double angle);
ITypeErasedFuture* turtle_save_state();
ITypeErasedFuture* turtle_restore_state();
size_t turtle_process_events();

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
