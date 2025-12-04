#include "robot.h"

namespace NQumir {
namespace NRuntime {

namespace {
    int robotX = 0;
    int robotY = 0;
    bool cellPainted = false;
}

extern "C" {

void robot_left() {
    robotX--;
}

void robot_right() {
    robotX++;
}

void robot_up() {
    robotY--;
}

void robot_down() {
    robotY++;
}

void robot_paint() {
    cellPainted = true;
}

bool robot_left_free() {
    return true;
}

bool robot_right_free() {
    return true;
}

bool robot_top_free() {
    return true;
}

bool robot_bottom_free() {
    return true;
}

bool robot_left_wall() {
    return !robot_left_free();
}

bool robot_right_wall() {
    return !robot_right_free();
}

bool robot_top_wall() {
    return !robot_top_free();
}

bool robot_bottom_wall() {
    return !robot_bottom_free();
}

bool robot_cell_painted() {
    return cellPainted;
}

bool robot_cell_clean() {
    return !robot_cell_painted();
}

double robot_radiation() {
    return 0.0;
}

double robot_temperature() {
    return 0.0;
}

}

}
}
