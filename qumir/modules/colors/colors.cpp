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
            .MangledName = "painter_transparent",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_transparent)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_transparent());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "белый",
            .MangledName = "painter_white",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_white)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_white());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "чёрный",
            .MangledName = "painter_black",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_black)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_black());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "черный",
            .MangledName = "painter_black",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_black)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_black());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "серый",
            .MangledName = "painter_gray",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_gray)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_gray());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "фиолетовый",
            .MangledName = "painter_purple",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_purple)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_purple());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "синий",
            .MangledName = "painter_blue",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_blue)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_blue());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "голубой",
            .MangledName = "painter_cyan",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_cyan)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_cyan());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "зелёный",
            .MangledName = "painter_green",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_green)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_green());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "зеленый",
            .MangledName = "painter_green",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_green)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_green());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "жёлтый",
            .MangledName = "painter_yellow",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_yellow)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_yellow());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "желтый",
            .MangledName = "painter_yellow",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_yellow)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_yellow());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "оранжевый",
            .MangledName = "painter_orange",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_orange)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_orange());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "красный",
            .MangledName = "painter_red",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(color_red)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_red());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },

        // ── Color construction ────────────────────────────────────────────────
        {
            .Name = "RGB",
            .MangledName = "painter_rgb",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_rgb)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_rgb(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "RGBA",
            .MangledName = "painter_rgba",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_rgba)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_rgba(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYK",
            .MangledName = "painter_cmyk",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_cmyk)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_cmyk(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYKA",
            .MangledName = "painter_cmyka",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t)>(color_cmyka)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_cmyka(args[0], args[1], args[2], args[3], args[4]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSL",
            .MangledName = "painter_hsl",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_hsl)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsl(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSLA",
            .MangledName = "painter_hsla",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(color_hsla)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsla(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSV",
            .MangledName = "painter_hsv",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(color_hsv)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(color_hsv(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSVA",
            .MangledName = "painter_hsva",
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
            .MangledName = "painter_decompose_rgb",
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
            .MangledName = "painter_decompose_cmyk",
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
            .MangledName = "painter_decompose_hsl",
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
            .MangledName = "painter_decompose_hsv",
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
    };
}

} // namespace NRegistry
} // namespace NQumir
