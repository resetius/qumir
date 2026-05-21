#include "robot.h"
#include <qumir/future.h>

#include <functional>
#include <iostream>
#include <utility>
#include <vector>

namespace NQumir {
namespace NRuntime {

namespace {
    int robotX = 0;
    int robotY = 0;
    bool cellPainted = false;

    struct TPendingEvent {
        std::function<void()> Callback;
        TFuture<void> Future;
    };

    std::vector<TPendingEvent> pendingEvents;

    ITypeErasedFuture* EnqueueRobotCall(std::function<void()> call) {
        auto promise = std::make_shared<TPromise<void>>();
        pendingEvents.emplace_back(TPendingEvent{
            .Callback = std::move(call),
            .Future = MakeExternalFuture<void>(promise)
        });
        return new TWrappedFuture<void>(MakeExternalFuture<void>(promise));
    }
}

extern "C" {

ITypeErasedFuture* robot_left() {
    return EnqueueRobotCall([]() {
        robotX--;
        std::cerr << "robot_left\n";
    });
}

ITypeErasedFuture* robot_right() {
    return EnqueueRobotCall([]() {
        robotX++;
        std::cerr << "robot_right\n";
    });
}

ITypeErasedFuture* robot_up() {
    return EnqueueRobotCall([]() {
        robotY--;
        std::cerr << "robot_up\n";
    });
}

ITypeErasedFuture* robot_down() {
    return EnqueueRobotCall([]() {
        robotY++;
        std::cerr << "robot_down\n";
    });
}

ITypeErasedFuture* robot_paint() {
    return EnqueueRobotCall([]() {
        cellPainted = true;
        std::cerr << "robot_paint\n";
    });
}

size_t robot_process_events() {
    auto events = std::move(pendingEvents);
    for (auto& event : events) {
        if (event.Callback) {
            event.Callback();
        }
        if (!event.Future.done()) {
            event.Future.resume();
        }
    }
    return events.size();
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
