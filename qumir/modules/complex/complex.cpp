#include "complex.h"

namespace NQumir {
namespace NRegistry {

ComplexModule::ComplexModule() {
    auto floatType   = std::make_shared<NAst::TFloatType>();
    auto intType     = std::make_shared<NAst::TIntegerType>();
    auto boolType    = std::make_shared<NAst::TBoolType>();

    // компл — struct { double re; double im; }
    auto complexUnderlying = std::make_shared<NAst::TStructType>(
        std::vector<std::pair<std::string, NAst::TTypePtr>>{
            {"re", floatType},
            {"im", floatType},
        }
    );
    auto complexType = std::make_shared<NAst::TNamedType>("компл");
    complexType->UnderlyingType = complexUnderlying;

    ExternalTypes_ = {
        {
            .Name = "компл",
            .Type = complexUnderlying,
        },
    };

    ExternalFunctions_ = {

        // ── Компоненты ───────────────────────────────────────────────────────
        {
            .Name = "Re",
            .MangledName = "complex_re",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "Im",
            .MangledName = "complex_im",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Геометрия ────────────────────────────────────────────────────────
        {
            .Name = "мод",
            .MangledName = "complex_abs",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "аргумент",
            .MangledName = "complex_arg",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Сопряжённое ─────────────────────────────────────────────────────
        {
            .Name = "сопряжённое",
            .MangledName = "complex_conj",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },
        {
            .Name = "сопряженное",
            .MangledName = "complex_conj",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },

        // ── Арифметические операторы ─────────────────────────────────────────
        {
            .Name = "+",
            .MangledName = "complex_add",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "-",
            .MangledName = "complex_sub",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "*",
            .MangledName = "complex_mul",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "/",
            .MangledName = "complex_div",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        // унарный минус
        {
            .Name = "neg",
            .MangledName = "complex_neg",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Операторы сравнения ──────────────────────────────────────────────
        {
            .Name = "==",
            .MangledName = "complex_eq",
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
        },
        {
            .Name = "!=",
            .MangledName = "complex_ne",
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
        },

        // ── Присваивание ─────────────────────────────────────────────────────
        {
            .Name = ":=",
            .MangledName = "complex_assign",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Прямые касты: вещ/цел → компл ───────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_from_float",
            .ArgTypes = { floatType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "cast",
            .MangledName = "complex_from_int",
            .ArgTypes = { intType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Обратные касты: компл → вещ/цел ─────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_to_float",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
            .IsOp = true,
        },
        {
            .Name = "cast",
            .MangledName = "complex_to_int",
            .ArgTypes = { complexType },
            .ReturnType = intType,
            .IsOp = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
