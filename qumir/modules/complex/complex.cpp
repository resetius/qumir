#include "complex.h"

namespace NQumir {
namespace NRegistry {

ComplexModule::ComplexModule() {
    auto floatType = std::make_shared<NAst::TFloatType>();

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
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "Im",
            .MangledName = "complex_im",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Геометрия ────────────────────────────────────────────────────────
        {
            .Name = "мод",
            .MangledName = "complex_abs",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "аргумент",
            .MangledName = "complex_arg",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Сопряжённое ─────────────────────────────────────────────────────
        {
            .Name = "сопряжённое",
            .MangledName = "complex_conj",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },
        {
            .Name = "сопряженное",
            .MangledName = "complex_conj",
            .Ptr = nullptr,
            .Packed = nullptr,
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
