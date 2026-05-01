#include "painter.h"

#include <qumir/runtime/painter.h>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

PainterModule::PainterModule() {
    auto integerType  = std::make_shared<NAst::TIntegerType>();
    auto boolType     = std::make_shared<NAst::TBoolType>();
    auto voidType     = std::make_shared<NAst::TVoidType>();
    auto stringType   = std::make_shared<NAst::TStringType>();
    auto intArrayType = std::make_shared<NAst::TArrayType>(integerType, 1);

    auto colorType = std::make_shared<NAst::TNamedType>("цвет");
    colorType->UnderlyingType = integerType;

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
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_transparent)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_transparent());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "белый",
            .MangledName = "painter_white",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_white)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_white());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "чёрный",
            .MangledName = "painter_black",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_black)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_black());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "черный",
            .MangledName = "painter_black",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_black)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_black());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "серый",
            .MangledName = "painter_gray",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_gray)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_gray());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "фиолетовый",
            .MangledName = "painter_purple",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_purple)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_purple());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "синий",
            .MangledName = "painter_blue",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_blue)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_blue());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "голубой",
            .MangledName = "painter_cyan",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_cyan)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_cyan());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "зелёный",
            .MangledName = "painter_green",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_green)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_green());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "зеленый",
            .MangledName = "painter_green",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_green)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_green());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "жёлтый",
            .MangledName = "painter_yellow",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_yellow)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_yellow());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "желтый",
            .MangledName = "painter_yellow",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_yellow)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_yellow());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "оранжевый",
            .MangledName = "painter_orange",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_orange)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_orange());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "красный",
            .MangledName = "painter_red",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_red)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_red());
            },
            .ArgTypes = {},
            .ReturnType = colorType,
        },

        // ── Color construction ────────────────────────────────────────────────
        {
            .Name = "RGB",
            .MangledName = "painter_rgb",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(painter_rgb)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_rgb(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "RGBA",
            .MangledName = "painter_rgba",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(painter_rgba)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_rgba(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYK",
            .MangledName = "painter_cmyk",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(painter_cmyk)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_cmyk(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYKA",
            .MangledName = "painter_cmyka",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t,int64_t)>(painter_cmyka)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_cmyka(args[0], args[1], args[2], args[3], args[4]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSL",
            .MangledName = "painter_hsl",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(painter_hsl)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_hsl(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSLA",
            .MangledName = "painter_hsla",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(painter_hsla)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_hsla(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSV",
            .MangledName = "painter_hsv",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t)>(painter_hsv)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_hsv(args[0], args[1], args[2]));
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSVA",
            .MangledName = "painter_hsva",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t,int64_t,int64_t)>(painter_hsva)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_hsva(args[0], args[1], args[2], args[3]));
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },

        // ── Color decomposition ───────────────────────────────────────────────
        {
            .Name = "разложить в RGB",
            .MangledName = "painter_decompose_rgb",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(painter_decompose_rgb)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_decompose_rgb(args[0],
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
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*,int64_t*)>(painter_decompose_cmyk)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_decompose_cmyk(args[0],
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
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(painter_decompose_hsl)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_decompose_hsl(args[0],
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
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*,int64_t*)>(painter_decompose_hsv)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_decompose_hsv(args[0],
                    reinterpret_cast<int64_t*>(args[1]),
                    reinterpret_cast<int64_t*>(args[2]),
                    reinterpret_cast<int64_t*>(args[3]));
                return 0;
            },
            .ArgTypes = { colorType, makeOutInt(), makeOutInt(), makeOutInt() },
            .ReturnType = voidType,
        },

        // ── Sheet info ────────────────────────────────────────────────────────
        {
            .Name = "высота листа",
            .MangledName = "painter_sheet_height",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_sheet_height)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_sheet_height());
            },
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "ширина листа",
            .MangledName = "painter_sheet_width",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_sheet_width)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_sheet_width());
            },
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "центр x",
            .MangledName = "painter_center_x",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_center_x)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_center_x());
            },
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "центр y",
            .MangledName = "painter_center_y",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(painter_center_y)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_center_y());
            },
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "ширина текста",
            .MangledName = "painter_text_width",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*)>(painter_text_width)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_text_width(reinterpret_cast<const char*>(args[0])));
            },
            .ArgTypes = { stringType },
            .ReturnType = integerType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "значение в точке",
            .MangledName = "painter_get_pixel",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t,int64_t)>(painter_get_pixel)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(painter_get_pixel(args[0], args[1]));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = colorType,
        },

        // ── Drawing parameters ────────────────────────────────────────────────
        {
            .Name = "перо",
            .MangledName = "painter_pen",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t)>(painter_pen)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_pen(args[0], args[1]);
                return 0;
            },
            .ArgTypes = { integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "кисть",
            .MangledName = "painter_brush",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(painter_brush)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_brush(args[0]);
                return 0;
            },
            .ArgTypes = { colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "убрать кисть",
            .MangledName = "painter_no_brush",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)()>(painter_no_brush)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_no_brush();
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "плотность",
            .MangledName = "painter_density",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(painter_density)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_density(args[0]);
                return 0;
            },
            .ArgTypes = { integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "шрифт",
            .MangledName = "painter_font",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(const char*,int64_t,bool,bool)>(painter_font)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_font(reinterpret_cast<const char*>(args[0]),
                             static_cast<int64_t>(args[1]),
                             static_cast<bool>(args[2]),
                             static_cast<bool>(args[3]));
                return 0;
            },
            .ArgTypes = { stringType, integerType, boolType, boolType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },

        // ── Drawing commands ──────────────────────────────────────────────────
        {
            .Name = "в точку",
            .MangledName = "painter_move_to",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t)>(painter_move_to)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_move_to(args[0], args[1]);
                return 0;
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "линия",
            .MangledName = "painter_line",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t,int64_t)>(painter_line)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_line(args[0], args[1], args[2], args[3]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "линия в точку",
            .MangledName = "painter_line_to",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t)>(painter_line_to)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_line_to(args[0], args[1]);
                return 0;
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "многоугольник",
            .MangledName = "painter_polygon",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t*,int64_t*)>(painter_polygon)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_polygon(static_cast<int64_t>(args[0]),
                                reinterpret_cast<int64_t*>(args[1]),
                                reinterpret_cast<int64_t*>(args[2]));
                return 0;
            },
            .ArgTypes = { integerType, intArrayType, intArrayType },
            .ReturnType = voidType,
        },
        {
            .Name = "пиксель",
            .MangledName = "painter_pixel",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t)>(painter_pixel)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_pixel(args[0], args[1], args[2]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "прямоугольник",
            .MangledName = "painter_rect",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t,int64_t)>(painter_rect)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_rect(args[0], args[1], args[2], args[3]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "эллипс",
            .MangledName = "painter_ellipse",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t,int64_t)>(painter_ellipse)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_ellipse(args[0], args[1], args[2], args[3]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "окружность",
            .MangledName = "painter_circle",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t)>(painter_circle)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_circle(args[0], args[1], args[2]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "надпись",
            .MangledName = "painter_text",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,const char*)>(painter_text)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_text(args[0], args[1], reinterpret_cast<const char*>(args[2]));
                return 0;
            },
            .ArgTypes = { integerType, integerType, stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "залить",
            .MangledName = "painter_fill",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t)>(painter_fill)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_fill(args[0], args[1]);
                return 0;
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },

        // ── Sheet management ──────────────────────────────────────────────────
        {
            .Name = "новый лист",
            .MangledName = "painter_new_sheet",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t,int64_t,int64_t)>(painter_new_sheet)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_new_sheet(args[0], args[1], args[2]);
                return 0;
            },
            .ArgTypes = { integerType, integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "загрузить лист",
            .MangledName = "painter_load_sheet",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(const char*)>(painter_load_sheet)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_load_sheet(reinterpret_cast<const char*>(args[0]));
                return 0;
            },
            .ArgTypes = { stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "сохранить лист",
            .MangledName = "painter_save_sheet",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(const char*)>(painter_save_sheet)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                painter_save_sheet(reinterpret_cast<const char*>(args[0]));
                return 0;
            },
            .ArgTypes = { stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
