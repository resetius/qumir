#include "complex.h"

#include <qumir/runtime/complex.h>

#include <bit>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;

ComplexModule::ComplexModule() {
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto intType   = std::make_shared<NAst::TIntegerType>();
    auto boolType  = std::make_shared<NAst::TBoolType>();

    auto complexUnderlying = std::make_shared<NAst::TStructType>(
        std::vector<std::pair<std::string, NAst::TTypePtr>>{
            {"re", floatType},
            {"im", floatType},
        }
    );
    auto complexType = std::make_shared<NAst::TNamedType>("компл", complexUnderlying);

    ExternalTypes_ = {
        {
            .Name = "компл",
            .Type = complexUnderlying,
        },
    };

    ExternalFunctions_ = {

        // ── Constants ────────────────────────────────────────────────────────
        {
            .Name = "i",
            .MangledName = "complex_i",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*)>(complex_i)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_i(reinterpret_cast<komplex*>(args[0]));
                return 0;
            },
            .ArgTypes = {},
            .ReturnType = complexType,
        },

        // ── Scalar accessors ──────────────────────────────────────────────────
        {
            .Name = "Re",
            .MangledName = "complex_re",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const komplex*)>(complex_re)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_re(reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "Im",
            .MangledName = "complex_im",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const komplex*)>(complex_im)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_im(reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Geometry ──────────────────────────────────────────────────────────
        {
            .Name = "мод",
            .MangledName = "complex_abs",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const komplex*)>(complex_abs)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_abs(reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "аргумент",
            .MangledName = "complex_arg",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const komplex*)>(complex_arg)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_arg(reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Сопряжённое ───────────────────────────────────────────────────────
        {
            .Name = "сопряжённое",
            .MangledName = "complex_conj",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*)>(complex_conj)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_conj(reinterpret_cast<komplex*>(args[0]),
                             reinterpret_cast<const komplex*>(args[1]));
                return 0;
            },
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },
        {
            .Name = "сопряженное",
            .MangledName = "complex_conj",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*)>(complex_conj)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_conj(reinterpret_cast<komplex*>(args[0]),
                             reinterpret_cast<const komplex*>(args[1]));
                return 0;
            },
            .ArgTypes = { complexType },
            .ReturnType = complexType,
        },

        // ── Binary operators ──────────────────────────────────────────────────
        {
            .Name = "+",
            .MangledName = "complex_add",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*, const komplex*)>(complex_add)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_add(reinterpret_cast<komplex*>(args[0]),
                            reinterpret_cast<const komplex*>(args[1]),
                            reinterpret_cast<const komplex*>(args[2]));
                return 0;
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "-",
            .MangledName = "complex_sub",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*, const komplex*)>(complex_sub)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_sub(reinterpret_cast<komplex*>(args[0]),
                            reinterpret_cast<const komplex*>(args[1]),
                            reinterpret_cast<const komplex*>(args[2]));
                return 0;
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "*",
            .MangledName = "complex_mul",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*, const komplex*)>(complex_mul)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_mul(reinterpret_cast<komplex*>(args[0]),
                            reinterpret_cast<const komplex*>(args[1]),
                            reinterpret_cast<const komplex*>(args[2]));
                return 0;
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "/",
            .MangledName = "complex_div",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*, const komplex*)>(complex_div)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_div(reinterpret_cast<komplex*>(args[0]),
                            reinterpret_cast<const komplex*>(args[1]),
                            reinterpret_cast<const komplex*>(args[2]));
                return 0;
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Unary minus ───────────────────────────────────────────────────────
        {
            .Name = "neg",
            .MangledName = "complex_neg",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, const komplex*)>(complex_neg)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_neg(reinterpret_cast<komplex*>(args[0]),
                            reinterpret_cast<const komplex*>(args[1]));
                return 0;
            },
            .ArgTypes = { complexType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Comparison ────────────────────────────────────────────────────────
        {
            .Name = "==",
            .MangledName = "complex_eq",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const komplex*, const komplex*)>(complex_eq)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(complex_eq(
                    reinterpret_cast<const komplex*>(args[0]),
                    reinterpret_cast<const komplex*>(args[1])));
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
        },
        {
            .Name = "!=",
            .MangledName = "complex_ne",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const komplex*, const komplex*)>(complex_ne)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(complex_ne(
                    reinterpret_cast<const komplex*>(args[0]),
                    reinterpret_cast<const komplex*>(args[1])));
            },
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
        },

        // ── Casts: вещ/цел → компл ────────────────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_from_float",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, double)>(complex_from_float)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_from_float(reinterpret_cast<komplex*>(args[0]),
                                   std::bit_cast<double>(args[1]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = complexType,
            .IsOp = true,
        },
        {
            .Name = "cast",
            .MangledName = "complex_from_int",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex*, int64_t)>(complex_from_int)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_from_int(reinterpret_cast<komplex*>(args[0]),
                                 static_cast<int64_t>(args[1]));
                return 0;
            },
            .ArgTypes = { intType },
            .ReturnType = complexType,
            .IsOp = true,
        },

        // ── Casts: компл → вещ/цел ────────────────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_to_float",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const komplex*)>(complex_to_float)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_to_float(reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
            .IsOp = true,
        },
        {
            .Name = "cast",
            .MangledName = "complex_to_int",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const komplex*)>(complex_to_int)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                return static_cast<uint64_t>(complex_to_int(
                    reinterpret_cast<const komplex*>(args[0])));
            },
            .ArgTypes = { complexType },
            .ReturnType = intType,
            .IsOp = true,
        },
    };
}

} // namespace NRegistry
} // namespace NQumir
