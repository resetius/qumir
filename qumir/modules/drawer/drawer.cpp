#include "drawer.h"

#include <qumir/runtime/drawer.h>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

DrawerModule::DrawerModule()
{
    auto integerType = std::make_shared<NAst::TIntegerType>();
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto voidType = std::make_shared<NAst::TVoidType>();
    auto stringType = std::make_shared<NAst::TStringType>();

    std::vector<TExternalFunction> functions = {
        {
            .Name = "поднять перо",
            .MangledName = "drawer_pen_up",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(drawer_pen_up)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_pen_up();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
        {
            .Name = "опустить перо",
            .MangledName = "drawer_pen_down",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(drawer_pen_down)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_pen_down();
                return 0;
            },
            .ArgTypes = {  },
            .ReturnType = voidType,
        },
        {
            .Name = "выбрать чернила",
            .MangledName = "drawer_set_color",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(drawer_set_color)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_set_color(static_cast<int64_t>(args[0]));
                return 0;
            },
            .ArgTypes = { integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "сместиться в точку",
            .MangledName = "drawer_move_to",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double, double)>(drawer_move_to)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_move_to(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1]));
                return 0;
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "сместиться на вектор",
            .MangledName = "drawer_move_by",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double, double)>(drawer_move_by)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_move_by(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1]));
                return 0;
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "написать",
            .MangledName = "drawer_write_text",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double, const char*)>(drawer_write_text)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                drawer_write_text(std::bit_cast<double>(args[0]), reinterpret_cast<const char*>(args[1]));
                return 0;
            },
            .ArgTypes = { floatType, stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
    };

    ExternalFunctions_.swap(functions);
}

} // namespace NRegistry
} // namespace NQumir
