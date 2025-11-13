#include "turtle.h"

#include <iostream>
#include <cmath>

#include <qumir/runtime/turtle.h>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

TurtleModule::TurtleModule()
{
    auto integerType = std::make_shared<NAst::TIntegerType>();
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto voidType = std::make_shared<NAst::TVoidType>();
    auto stringType = std::make_shared<NAst::TStringType>();
    auto voidPtrType = std::make_shared<NAst::TPointerType>(voidType);

    std::vector<TExternalFunction> functions = {
        {
            .Name = "поднять хвост",
            .MangledName = "turtle_pen_up",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(turtle_pen_up)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_pen_up();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
        {
            .Name = "опустить хвост",
            .MangledName = "turtle_pen_down",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(turtle_pen_down)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_pen_down();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
        {
            .Name = "вперед",
            .MangledName = "turtle_forward",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double)>(turtle_forward)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_forward(std::bit_cast<double>(args[0]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "назад",
            .MangledName = "turtle_backward",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double)>(turtle_backward)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_backward(std::bit_cast<double>(args[0]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "влево",
            .MangledName = "turtle_turn_left",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double)>(turtle_turn_left)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_turn_left(std::bit_cast<double>(args[0]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "вправо",
            .MangledName = "turtle_turn_right",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double)>(turtle_turn_right)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_turn_right(std::bit_cast<double>(args[0]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "сохранить состояние",
            .MangledName = "turtle_save_state",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(turtle_save_state)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_save_state();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
        {
            .Name = "восстановить состояние",
            .MangledName = "turtle_restore_state",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(turtle_restore_state)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                turtle_restore_state();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
    };

    ExternalFunctions_.swap(functions);
}

} // namespace NRegistry
} // namespace NQumir