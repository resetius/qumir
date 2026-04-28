#include "painter.h"

namespace NQumir {
namespace NRegistry {

PainterModule::PainterModule() {
    auto integerType  = std::make_shared<NAst::TIntegerType>();
    auto boolType     = std::make_shared<NAst::TBoolType>();
    auto voidType     = std::make_shared<NAst::TVoidType>();
    auto stringType   = std::make_shared<NAst::TStringType>();
    auto intArrayType = std::make_shared<NAst::TArrayType>(integerType, 1);

    // цвет: named type wrapping integer, pre-resolved so the codegen sees the underlying type
    auto colorType = std::make_shared<NAst::TNamedType>("цвет");
    colorType->UnderlyingType = integerType;

    // аргрез цел — mutable+readable reference (for decomposition functions)
    auto makeInoutInt = [&]() -> NAst::TTypePtr {
        auto t = std::make_shared<NAst::TIntegerType>();
        t->Mutable  = true;
        t->Readable = true;
        return std::make_shared<NAst::TReferenceType>(t);
    };

    ExternalTypes_ = {
        {
            .Name = "цвет",
            .Type = integerType,
        },
    };

    ExternalFunctions_ = {

        // ── Color constants (no args) ─────────────────────────────────────────
        {
            .Name = "прозрачный",
            .MangledName = "painter_transparent",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "белый",
            .MangledName = "painter_white",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "чёрный",
            .MangledName = "painter_black",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "серый",
            .MangledName = "painter_gray",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "фиолетовый",
            .MangledName = "painter_purple",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "синий",
            .MangledName = "painter_blue",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "голубой",
            .MangledName = "painter_cyan",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "зелёный",
            .MangledName = "painter_green",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "жёлтый",
            .MangledName = "painter_yellow",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "желтый",
            .MangledName = "painter_yellow",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "оранжевый",
            .MangledName = "painter_orange",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },
        {
            .Name = "красный",
            .MangledName = "painter_red",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = colorType,
        },

        // ── Color construction ────────────────────────────────────────────────
        {
            .Name = "RGB",
            .MangledName = "painter_rgb",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "RGBA",
            .MangledName = "painter_rgba",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYK",
            .MangledName = "painter_cmyk",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "CMYKA",
            .MangledName = "painter_cmyka",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSL",
            .MangledName = "painter_hsl",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSLA",
            .MangledName = "painter_hsla",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSV",
            .MangledName = "painter_hsv",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = colorType,
        },
        {
            .Name = "HSVA",
            .MangledName = "painter_hsva",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = colorType,
        },

        // ── Color decomposition ───────────────────────────────────────────────
        {
            .Name = "разложить в RGB",
            .MangledName = "painter_decompose_rgb",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { colorType, makeInoutInt(), makeInoutInt(), makeInoutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в CMYK",
            .MangledName = "painter_decompose_cmyk",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { colorType, makeInoutInt(), makeInoutInt(), makeInoutInt(), makeInoutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в HSL",
            .MangledName = "painter_decompose_hsl",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { colorType, makeInoutInt(), makeInoutInt(), makeInoutInt() },
            .ReturnType = voidType,
        },
        {
            .Name = "разложить в HSV",
            .MangledName = "painter_decompose_hsv",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { colorType, makeInoutInt(), makeInoutInt(), makeInoutInt() },
            .ReturnType = voidType,
        },

        // ── Sheet info ────────────────────────────────────────────────────────
        {
            .Name = "высота листа",
            .MangledName = "painter_sheet_height",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "ширина листа",
            .MangledName = "painter_sheet_width",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "центр x",
            .MangledName = "painter_center_x",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "центр y",
            .MangledName = "painter_center_y",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = integerType,
        },
        {
            .Name = "ширина текста",
            .MangledName = "painter_text_width",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { stringType },
            .ReturnType = integerType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "значение в точке",
            .MangledName = "painter_get_pixel",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType },
            .ReturnType = colorType,
        },

        // ── Drawing parameters ────────────────────────────────────────────────
        {
            .Name = "перо",
            .MangledName = "painter_pen",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "кисть",
            .MangledName = "painter_brush",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "убрать кисть",
            .MangledName = "painter_no_brush",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = {},
            .ReturnType = voidType,
        },
        {
            .Name = "плотность",
            .MangledName = "painter_density",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "шрифт",
            .MangledName = "painter_font",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { stringType, integerType, boolType, boolType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },

        // ── Drawing commands ──────────────────────────────────────────────────
        {
            .Name = "в точку",
            .MangledName = "painter_move_to",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "линия",
            .MangledName = "painter_line",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "линия в точку",
            .MangledName = "painter_line_to",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "многоугольник",
            .MangledName = "painter_polygon",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, intArrayType, intArrayType },
            .ReturnType = voidType,
        },
        {
            .Name = "пиксель",
            .MangledName = "painter_pixel",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "прямоугольник",
            .MangledName = "painter_rect",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "эллипс",
            .MangledName = "painter_ellipse",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "окружность",
            .MangledName = "painter_circle",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "надпись",
            .MangledName = "painter_text",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "залить",
            .MangledName = "painter_fill",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType },
            .ReturnType = voidType,
        },

        // ── Sheet management ──────────────────────────────────────────────────
        {
            .Name = "новый лист",
            .MangledName = "painter_new_sheet",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { integerType, integerType, colorType },
            .ReturnType = voidType,
        },
        {
            .Name = "загрузить лист",
            .MangledName = "painter_load_sheet",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
        {
            .Name = "сохранить лист",
            .MangledName = "painter_save_sheet",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { stringType },
            .ReturnType = voidType,
            .RequireArgsMaterialization = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
