#include "colors.h"

#include <qumir/runtime/colors.h>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

ColorsModule::ColorsModule() {
    auto integerType  = std::make_shared<NAst::TIntegerType>();
    auto boolType     = std::make_shared<NAst::TBoolType>();
    auto voidType     = std::make_shared<NAst::TVoidType>();
    auto stringType   = std::make_shared<NAst::TStringType>();
    auto intArrayType = std::make_shared<NAst::TArrayType>(integerType, 1);

    auto colorType = std::make_shared<NAst::TNamedType>("цвет", integerType);

    auto colorConst = [colorType](int64_t value) {
        return [colorType, value](std::vector<NAst::TExprPtr>) -> NAst::TExprPtr {
            auto expr = std::make_shared<NAst::TNumberExpr>(TLocation{}, value);
            expr->Type = colorType;
            return expr;
        };
    };

    auto makeOutInt = [&]() -> NAst::TTypePtr {
        auto t = std::make_shared<NAst::TIntegerType>();
        t->Mutable  = true;
        t->Readable = false;
        return std::make_shared<NAst::TReferenceType>(t);
    };

    ExternalTypes_ = {
        {
            .Name = "цвет",
            .Type = integerType,
        },
    };

    ExternalFunctions_ = {

        // ── Color constants ───────────────────────────────────────────────────
        {
            .Name = "прозрачный",
            .MangledName = "color_transparent",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackARGB(0, 0, 0, 0)),
        },
        {
            .Name = "белый",
            .MangledName = "color_white",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 255)),
        },
        {
            .Name = "чёрный",
            .MangledName = "color_black",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 0)),
        },
        {
            .Name = "черный",
            .MangledName = "color_black",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 0)),
        },
        {
            .Name = "серый",
            .MangledName = "color_gray",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(128, 128, 128)),
        },
        {
            .Name = "фиолетовый",
            .MangledName = "color_purple",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(128, 0, 128)),
        },
        {
            .Name = "синий",
            .MangledName = "color_blue",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 0, 255)),
        },
        {
            .Name = "голубой",
            .MangledName = "color_cyan",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 255, 255)),
        },
        {
            .Name = "зелёный",
            .MangledName = "color_green",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 128, 0)),
        },
        {
            .Name = "зеленый",
            .MangledName = "color_green",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(0, 128, 0)),
        },
        {
            .Name = "жёлтый",
            .MangledName = "color_yellow",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 0)),
        },
        {
            .Name = "желтый",
            .MangledName = "color_yellow",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 255, 0)),
        },
        {
            .Name = "оранжевый",
            .MangledName = "color_orange",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 165, 0)),
        },
        {
            .Name = "красный",
            .MangledName = "color_red",
            .ArgTypes = {},
            .ReturnType = colorType,
            .Inline = colorConst(PackRGB(255, 0, 0)),
        },

        // ── Color construction ────────────────────────────────────────────────
        {
            .Name = "RGB",
            .MangledName = "color_rgb",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_rgb)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_rgb(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "RGBA",
            .MangledName = "color_rgba",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_rgba)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_rgba(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYK",
            .MangledName = "color_cmyk",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_cmyk)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_cmyk(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYKA",
            .MangledName = "color_cmyka",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t)>(color_cmyka)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_cmyka(args[0], args[1], args[2], args[3], args[4]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSL",
            .MangledName = "color_hsl",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_hsl)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsl(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSLA",
            .MangledName = "color_hsla",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_hsla)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsla(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSV",
            .MangledName = "color_hsv",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_hsv)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsv(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSVA",
            .MangledName = "color_hsva",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_hsva)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsva(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },

        // ── Color decomposition ───────────────────────────────────────────────
        {
            .Name = "разложить в RGB",
            .MangledName = "color_decompose_rgb",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(color_decompose_rgb)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_decompose_rgb(args[0],
                    reinterpret_cast<int64_t*>(args[1]),
                    reinterpret_cast<int64_t*>(args[2]),
                    reinterpret_cast<int64_t*>(args[3]));
                return 0;
            },
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в CMYK",
            .MangledName = "color_decompose_cmyk",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*,int64_t*)>(color_decompose_cmyk)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_decompose_cmyk(args[0],
                    reinterpret_cast<int64_t*>(args[1]),
                    reinterpret_cast<int64_t*>(args[2]),
                    reinterpret_cast<int64_t*>(args[3]),
                    reinterpret_cast<int64_t*>(args[4]));
                return 0;
            },
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в HSL",
            .MangledName = "color_decompose_hsl",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(color_decompose_hsl)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_decompose_hsl(args[0],
                    reinterpret_cast<int64_t*>(args[1]),
                    reinterpret_cast<int64_t*>(args[2]),
                    reinterpret_cast<int64_t*>(args[3]));
                return 0;
            },
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в HSV",
            .MangledName = "color_decompose_hsv",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(color_decompose_hsv)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_decompose_hsv(args[0],
                    reinterpret_cast<int64_t*>(args[1]),
                    reinterpret_cast<int64_t*>(args[2]),
                    reinterpret_cast<int64_t*>(args[3]));
                return 0;
            },
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
        },

        // ── Output operator ───────────────────────────────────────────────────
        {
            .Name = "вывод",
            .MangledName = "color_print",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(color_print)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                color_print(static_cast<int64_t>(args[0]));
                return 0;
            },
            .ArgTypes = { colorType },
            .ReturnType = voidType,
            .IsOp = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
