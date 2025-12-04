#include "robot.h"

#include <qumir/runtime/robot.h>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

RobotModule::RobotModule()
{
    auto boolType = std::make_shared<NAst::TBoolType>();
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto voidType = std::make_shared<NAst::TVoidType>();

    std::vector<TExternalFunction> functions = {
        {
            .Name = "влево",
            .MangledName = "robot_left",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(robot_left)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                robot_left();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "вправо",
            .MangledName = "robot_right",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(robot_right)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                robot_right();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "вверх",
            .MangledName = "robot_up",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(robot_up)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                robot_up();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "вниз",
            .MangledName = "robot_down",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(robot_down)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                robot_down();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "закрасить",
            .MangledName = "robot_paint",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(robot_paint)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                robot_paint();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "слева свободно",
            .MangledName = "robot_left_free",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_left_free)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_left_free() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "справа свободно",
            .MangledName = "robot_right_free",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_right_free)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_right_free() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "сверху свободно",
            .MangledName = "robot_top_free",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_top_free)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_top_free() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "снизу свободно",
            .MangledName = "robot_bottom_free",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_bottom_free)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_bottom_free() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "слева стена",
            .MangledName = "robot_left_wall",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_left_wall)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_left_wall() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "справа стена",
            .MangledName = "robot_right_wall",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_right_wall)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_right_wall() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "сверху стена",
            .MangledName = "robot_top_wall",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_top_wall)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_top_wall() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "снизу стена",
            .MangledName = "robot_bottom_wall",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_bottom_wall)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_bottom_wall() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "клетка закрашена",
            .MangledName = "robot_cell_painted",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_cell_painted)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_cell_painted() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "клетка чистая",
            .MangledName = "robot_cell_clean",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)()>(robot_cell_clean)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return robot_cell_clean() ? 1 : 0;
            },
            .ArgTypes = {},
            .ReturnType = boolType,
        },
        {
            .Name = "радиация",
            .MangledName = "robot_radiation",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)()>(robot_radiation)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(robot_radiation());
            },
            .ArgTypes = {},
            .ReturnType = floatType,
        },
        {
            .Name = "температура",
            .MangledName = "robot_temperature",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)()>(robot_temperature)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(robot_temperature());
            },
            .ArgTypes = {},
            .ReturnType = floatType,
        },
    };

    ExternalFunctions_.swap(functions);
}

} // namespace NRegistry
} // namespace NQumir
