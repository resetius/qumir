#include "drawer.h"

#include <iostream>

namespace NQumir {
namespace NRuntime {

namespace {
    double current_x = 0.0;
    double current_y = 0.0;
    bool pen_down = true;
    int64_t current_color = 0; // черный по умолчанию
}

extern "C" {

void drawer_pen_up() {
    std::cerr << "Drawer pen up\n";
    pen_down = false;
}

void drawer_pen_down() {
    std::cerr << "Drawer pen down\n";
    pen_down = true;
}

void drawer_set_color(int64_t color) {
    std::cerr << "Drawer set color to " << color << "\n";
    current_color = color;
}

void drawer_move_to(double x, double y) {
    std::cerr << "Drawer move to (" << x << ", " << y << ")\n";
    if (pen_down) {
        std::cerr << "Drawing from (" << current_x << ", " << current_y << ") to (" << x << ", " << y << ") with color " << current_color << "\n";
    } else {
        std::cerr << "Moving from (" << current_x << ", " << current_y << ") to (" << x << ", " << y << ")\n";
    }
    current_x = x;
    current_y = y;
}

void drawer_move_by(double dx, double dy) {
    double new_x = current_x + dx;
    double new_y = current_y + dy;
    std::cerr << "Drawer move by (" << dx << ", " << dy << ")\n";
    if (pen_down) {
        std::cerr << "Drawing from (" << current_x << ", " << current_y << ") to (" << new_x << ", " << new_y << ") with color " << current_color << "\n";
    } else {
        std::cerr << "Moving from (" << current_x << ", " << current_y << ") to (" << new_x << ", " << new_y << ")\n";
    }
    current_x = new_x;
    current_y = new_y;
}

void drawer_write_text(double width, const char* text) {
    std::cerr << "Drawer write text '" << text << "' with width " << width << " at (" << current_x << ", " << current_y << ")\n";
}

} // extern "C"

} // namespace NRuntime
} // namespace NQumir
