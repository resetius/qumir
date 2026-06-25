#include "complex.h"

#include <qumir/runtime/complex.h>

#include <bit>

namespace NQumir {
namespace NRegistry {

using namespace NRuntime;
using namespace NAst;

namespace {

TExprPtr field(TExprPtr obj, const std::string& name) {
    return std::make_shared<TFieldAccessExpr>(obj->Location, obj, name);
}

TExprPtr binop(const char* op, TExprPtr l, TExprPtr r) {
    return std::make_shared<TBinaryExpr>(l->Location, TOperator(op), l, r);
}

// namedType must be the TNamedType wrapping the struct (e.g. TNamedType("компл", underlying)).
// The lowering unwraps it via UnwrapNamedType to access field offsets.
TExprPtr makeComplex(TTypePtr namedType, TExprPtr re, TExprPtr im) {
    auto loc = re->Location;
    return std::make_shared<TStructConstructExpr>(loc, std::move(namedType),
        std::vector<TExprPtr>{std::move(re), std::move(im)});
}

} // namespace

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
            .ArgTypes = {},
            .ReturnType = complexType,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                auto loc = TLocation{};
                return makeComplex(complexType,
                    std::make_shared<TNumberExpr>(loc, 0.0),
                    std::make_shared<TNumberExpr>(loc, 1.0));
            },
        },

        // ── Scalar accessors ──────────────────────────────────────────────────
        {
            .Name = "Re",
            .MangledName = "complex_re",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
            .Inline = [](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
                return std::make_shared<NAst::TFieldAccessExpr>(args[0]->Location, args[0], "re");
            },
        },
        {
            .Name = "Im",
            .MangledName = "complex_im",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
            .Inline = [](std::vector<NAst::TExprPtr> args) -> NAst::TExprPtr {
                return std::make_shared<NAst::TFieldAccessExpr>(args[0]->Location, args[0], "im");
            },
        },

        // ── Geometry ──────────────────────────────────────────────────────────
        {
            .Name = "мод",
            .MangledName = "complex_abs",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(komplex)>(complex_abs)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_abs(*reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },
        {
            .Name = "аргумент",
            .MangledName = "complex_arg",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(komplex)>(complex_arg)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                double r = complex_arg(*reinterpret_cast<const komplex*>(args[0]));
                return std::bit_cast<uint64_t>(r);
            },
            .ArgTypes = { complexType },
            .ReturnType = floatType,
        },

        // ── Сопряжённое ───────────────────────────────────────────────────────
        {
            .Name = "сопряжённое",
            .MangledName = "complex_conj",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType, field(args[0], "re"),
                    std::make_shared<TUnaryExpr>(args[0]->Location, TOperator("-"), field(args[0], "im")));
            },
        },
        {
            .Name = "сопряженное",
            .MangledName = "complex_conj",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType, field(args[0], "re"),
                    std::make_shared<TUnaryExpr>(args[0]->Location, TOperator("-"), field(args[0], "im")));
            },
        },

        // ── Binary operators ──────────────────────────────────────────────────
        {
            .Name = "+",
            .MangledName = "complex_add",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType,
                    binop("+", field(args[0],"re"), field(args[1],"re")),
                    binop("+", field(args[0],"im"), field(args[1],"im")));
            },
        },
        {
            .Name = "-",
            .MangledName = "complex_sub",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType,
                    binop("-", field(args[0],"re"), field(args[1],"re")),
                    binop("-", field(args[0],"im"), field(args[1],"im")));
            },
        },
        {
            .Name = "*",
            .MangledName = "complex_mul",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                // re = a.re*b.re - a.im*b.im
                // im = a.re*b.im + a.im*b.re
                return makeComplex(complexType,
                    binop("-", binop("*", field(args[0],"re"), field(args[1],"re")),
                               binop("*", field(args[0],"im"), field(args[1],"im"))),
                    binop("+", binop("*", field(args[0],"re"), field(args[1],"im")),
                               binop("*", field(args[0],"im"), field(args[1],"re"))));
            },
        },
        {
            .Name = "/",
            .MangledName = "complex_div",
            .ArgTypes = { complexType, complexType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                // re = (a.re*b.re + a.im*b.im) / (b.re^2 + b.im^2)
                // im = (a.im*b.re - a.re*b.im) / (b.re^2 + b.im^2)
                // denominator is computed twice — acceptable without optimiser
                auto denom = [&]{ return binop("+",
                    binop("*", field(args[1],"re"), field(args[1],"re")),
                    binop("*", field(args[1],"im"), field(args[1],"im"))); };
                return makeComplex(complexType,
                    binop("/", binop("+", binop("*", field(args[0],"re"), field(args[1],"re")),
                                        binop("*", field(args[0],"im"), field(args[1],"im"))),
                               denom()),
                    binop("/", binop("-", binop("*", field(args[0],"im"), field(args[1],"re")),
                                        binop("*", field(args[0],"re"), field(args[1],"im"))),
                               denom()));
            },
        },

        // ── Unary minus ───────────────────────────────────────────────────────
        {
            .Name = "neg",
            .MangledName = "complex_neg",
            .ArgTypes = { complexType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                auto loc = args[0]->Location;
                return makeComplex(complexType,
                    std::make_shared<TUnaryExpr>(loc, TOperator("-"), field(args[0],"re")),
                    std::make_shared<TUnaryExpr>(loc, TOperator("-"), field(args[0],"im")));
            },
        },

        // ── Comparison ────────────────────────────────────────────────────────
        {
            .Name = "==",
            .MangledName = "complex_eq",
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
            .Inline = [](std::vector<TExprPtr> args) -> TExprPtr {
                return binop("&&",
                    binop("==", field(args[0], "re"), field(args[1], "re")),
                    binop("==", field(args[0], "im"), field(args[1], "im")));
            },
        },
        {
            .Name = "!=",
            .MangledName = "complex_ne",
            .ArgTypes = { complexType, complexType },
            .ReturnType = boolType,
            .IsOp = true,
            .Inline = [](std::vector<TExprPtr> args) -> TExprPtr {
                return binop("||",
                    binop("!=", field(args[0], "re"), field(args[1], "re")),
                    binop("!=", field(args[0], "im"), field(args[1], "im")));
            },
        },

        // ── Imaginary literal suffix: 2i → __imag(2) ────────────────────────
        {
            .Name = "__imag",
            .MangledName = "complex_from_imag",
            .ArgTypes = { floatType },
            .ReturnType = complexType,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType,
                    std::make_shared<TNumberExpr>(args[0]->Location, 0.0), args[0]);
            },
        },

        // ── Casts: вещ/цел → компл ────────────────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_from_float",
            .ArgTypes = { floatType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType](std::vector<TExprPtr> args) -> TExprPtr {
                return makeComplex(complexType,
                    args[0], std::make_shared<TNumberExpr>(args[0]->Location, 0.0));
            },
        },
        {
            .Name = "cast",
            .MangledName = "complex_from_int",
            .ArgTypes = { intType },
            .ReturnType = complexType,
            .IsOp = true,
            .Inline = [complexType, floatType](std::vector<TExprPtr> args) -> TExprPtr {
                // cast int → float for re, im = 0
                return makeComplex(complexType,
                    MakeCast(args[0], floatType),
                    std::make_shared<TNumberExpr>(args[0]->Location, 0.0));
            },
        },

        // ── Casts: компл → вещ/цел ────────────────────────────────────────────
        {
            .Name = "cast",
            .MangledName = "complex_to_float",
            .ArgTypes = { complexType },
            .ReturnType = floatType,
            .IsOp = true,
            .Inline = [](std::vector<TExprPtr> args) -> TExprPtr {
                return field(args[0], "re");
            },
        },
        {
            .Name = "cast",
            .MangledName = "complex_to_int",
            .ArgTypes = { complexType },
            .ReturnType = intType,
            .IsOp = true,
            .Inline = [intType](std::vector<TExprPtr> args) -> TExprPtr {
                return std::make_shared<TCastExpr>(args[0]->Location, field(args[0], "re"), intType);
            },
        },

        // ── Output operator ───────────────────────────────────────────────────
        {
            .Name = "print",
            .MangledName = "complex_print",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(komplex)>(complex_print)),
            .Packed = +[](const uint64_t* args, size_t) -> uint64_t {
                complex_print(*reinterpret_cast<const komplex*>(args[0]));
                return 0;
            },
            .ArgTypes = { complexType },
            .ReturnType = std::make_shared<NAst::TVoidType>(),
            .IsOp = true,
        },
    };

    LiteralSuffixes_ = {
        { .Suffix = "i", .CtorFunction = "__imag" },
    };
}

} // namespace NRegistry
} // namespace NQumir
