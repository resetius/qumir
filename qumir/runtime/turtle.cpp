#include "turtle.h"

#include <iostream>
#include <cmath>
#include <vector>

namespace NQumir {
namespace NRuntime {

namespace {
    struct State {
        double x;
        double y;
        double angle;
        bool pen;
    };

    double current_x = 0.0;
    double current_y = 0.0;
    double angle_deg = 0.0; // 0 degrees is to the right
    bool pen_down = true;

    std::vector<State> states;
}

extern "C" {
// placeholders for turtle functions
void turtle_pen_up() {
    std::cerr << "Turtle pen up\n";
    pen_down = false;
}

void turtle_pen_down() {
    std::cerr << "Turtle pen down\n";
    pen_down = true;
}

void turtle_forward(double distance) {
    std::cerr << "Turtle forward " << distance << "\n";
    auto next_x = current_x + distance * cos(angle_deg * M_PI / 180.0);
    auto next_y = current_y + distance * sin(angle_deg * M_PI / 180.0);
    if (pen_down) {
        std::cerr << "Drawing from (" << current_x << "," << current_y << ") to (" << next_x << "," << next_y << ")\n";
    } else {
        std::cerr << "Moving  from (" << current_x << "," << current_y << ") to (" << next_x << "," << next_y << ")\n";
    }
    current_x = next_x;
    current_y = next_y;
}

void turtle_backward(double distance) {
    std::cerr << "Turtle backward " << distance << "\n";
    auto next_x = current_x - distance * cos(angle_deg * M_PI / 180.0);
    auto next_y = current_y - distance * sin(angle_deg * M_PI / 180.0);
    if (pen_down) {
        std::cerr << "Drawing from (" << current_x << "," << current_y << ") to (" << next_x << "," << next_y << ")\n";
    } else {
        std::cerr << "Moving  from (" << current_x << "," << current_y << ") to (" << next_x << "," << next_y << ")\n";
    }
    current_x = next_x;
    current_y = next_y;
}

void turtle_turn_left(double angle) {
    std::cerr << "Turtle turn left " << angle << "\n";
    angle_deg -= angle;
    std::cerr << "New angle: " << angle_deg << "\n";
}

void turtle_turn_right(double angle) {
    std::cerr << "Turtle turn right " << angle << "\n";
    angle_deg += angle;
    std::cerr << "New angle: " << angle_deg << "\n";
}

void turtle_save_state() {
    std::cerr << "Turtle save state\n";
    states.push_back(State{current_x, current_y, angle_deg, pen_down});
}

void turtle_restore_state() {
    std::cerr << "Turtle restore state\n";
    if (!states.empty()) {
        State s = states.back();
        states.pop_back();
        current_x = s.x;
        current_y = s.y;
        angle_deg = s.angle;
        pen_down = s.pen;
    } else {
        std::cerr << "No saved state to restore\n";
    }
}


} // extern "C"

} // namespace NRuntime
} // namespace NQumir
