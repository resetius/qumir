#include "system.h"

#include <iostream>

#include <stdlib.h>
#include <math.h>

#include <qumir/runtime/runtime.h>

namespace NQumir {
namespace NRegistry {

namespace {

struct TExternalFunction {
    std::string Name;
    std::string MangledName;
    void* Ptr;
    using TPacked = uint64_t(*)(const uint64_t* args, size_t argCount);
    TPacked Packed = nullptr; // packed thunk
    std::vector<NAst::TTypePtr> ArgTypes;
    NAst::TTypePtr ReturnType;
};

} // namespace

void SystemModule::Register(NSemantics::TNameResolver& ctx) {
    auto integerType = std::make_shared<NAst::TIntegerType>();
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto voidType = std::make_shared<NAst::TVoidType>();
    auto stringType = std::make_shared<NAst::TStringType>();

    std::vector<TExternalFunction> functions = {
        {
            .Name = "sign",
            .MangledName = "sign",
            .Ptr = reinterpret_cast<void*>(static_cast<int(*)(double)>(sign)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return sign(std::bit_cast<double>(args[0]));
            },
            .ArgTypes = { floatType },
            .ReturnType = integerType,
        },
        {
            .Name = "imin",
            .MangledName = "min_int64_t",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, int64_t)>(min_int64_t)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(min_int64_t(std::bit_cast<int64_t>(args[0]), std::bit_cast<int64_t>(args[1])));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "imax",
            .MangledName = "max_int64_t",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, int64_t)>(max_int64_t)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(max_int64_t(std::bit_cast<int64_t>(args[0]), std::bit_cast<int64_t>(args[1])));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "min",
            .MangledName = "min_double",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double, double)>(min_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(min_double(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1])));
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "max",
            .MangledName = "max_double",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double, double)>(max_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(max_double(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1])));
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "sqrt",
            .MangledName = "sqrt",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(sqrt)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(sqrt(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "iabs",
            .MangledName = "labs",
            .Ptr = reinterpret_cast<void*>(static_cast<int(*)(int)>(abs)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(labs(std::bit_cast<int64_t>(args[0])));
            },
            .ArgTypes = { integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "abs",
            .MangledName = "fabs",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(fabs)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(fabs(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "sin",
            .MangledName = "sin",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(sin)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(sin(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "cos",
            .MangledName = "cos",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(cos)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(cos(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "tg",
            .MangledName = "tan",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(tan)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(tan(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "ctg",
            .MangledName = "cotan",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(cotan)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(cotan(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "arcsin",
            .MangledName = "asin",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(asin)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(asin(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "arccos",
            .MangledName = "acos",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(acos)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(acos(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "arctg",
            .MangledName = "atan",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(atan)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(atan(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "ln",
            .MangledName = "log",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(log)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(log(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "lg",
            .MangledName = "log10",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(log10)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(log10(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "exp",
            .MangledName = "exp",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(exp)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(exp(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "div",
            .MangledName = "div_qum",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, int64_t)>(div_qum)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(div_qum(std::bit_cast<int64_t>(args[0]), std::bit_cast<int64_t>(args[1])));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "mod",
            .MangledName = "mod_qum",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, int64_t)>(mod_qum)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(mod_qum(std::bit_cast<int64_t>(args[0]), std::bit_cast<int64_t>(args[1])));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "fpow",
            .MangledName = "fpow",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double, int)>(fpow)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(fpow(std::bit_cast<double>(args[0]), static_cast<int>(std::bit_cast<int64_t>(args[1]))));
            },
            .ArgTypes = { floatType, integerType },
            .ReturnType = floatType,
        },
        {
            .Name = "pow",
            .MangledName = "pow",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double, double)>(pow)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(pow(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1])));
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "int",
            .MangledName = "trunc_double",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(double)>(trunc_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(trunc_double(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = integerType,
        },
        {
            .Name = "rnd",
            .MangledName = "rand_double",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(rand_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_double(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "rand",
            .MangledName = "rand_double_range",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double, double)>(rand_double_range)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_double_range(std::bit_cast<double>(args[0]), std::bit_cast<double>(args[1])));
            },
            .ArgTypes = { floatType, floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "irnd",
            .MangledName = "rand_uint64",
            .Ptr = reinterpret_cast<void*>(static_cast<uint64_t(*)(uint64_t)>(rand_uint64)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_uint64(std::bit_cast<uint64_t>(args[0])));
            },
            .ArgTypes = { integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "irand",
            .MangledName = "rand_uint64_range",
            .Ptr = reinterpret_cast<void*>(static_cast<uint64_t(*)(uint64_t, uint64_t)>(rand_uint64_range)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_uint64_range(std::bit_cast<uint64_t>(args[0]), std::bit_cast<uint64_t>(args[1])));
            },
            .ArgTypes = { integerType, integerType },
            .ReturnType = integerType,
        },

        // io
        {
            .Name = "input_double",
            .MangledName = "input_double",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)()>(NRuntime::input_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(NRuntime::input_double());
            },
            .ArgTypes = {  },
            .ReturnType = floatType,
        },
        {
            .Name = "input_int64",
            .MangledName = "input_int64",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)()>(NRuntime::input_int64)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(NRuntime::input_int64());
            },
            .ArgTypes = {  },
            .ReturnType = integerType,
        },
        {
            .Name = "output_double",
            .MangledName = "output_double",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(double)>(NRuntime::output_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::output_double(std::bit_cast<double>(args[0]));
                return 0;
            },
            .ArgTypes = { floatType },
            .ReturnType = voidType,
        },
        {
            .Name = "output_int64",
            .MangledName = "output_int64",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(NRuntime::output_int64)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::output_int64(std::bit_cast<int64_t>(args[0]));
                return 0;
            },
            .ArgTypes = { integerType },
            .ReturnType = voidType,
        },
        {
            .Name = "output_string",
            .MangledName = "output_string",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(const char*)>(NRuntime::output_string)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::output_string(reinterpret_cast<const char*>(args[0]));
                return 0;
            },
            // TODO:
            .ArgTypes = { stringType },
            .ReturnType = voidType,
        },

        // strings
        {
            .Name = "str_from_lit",
            .MangledName = "str_from_lit",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(const char*)>(NRuntime::str_from_lit)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto* str = NRuntime::str_from_lit(reinterpret_cast<const char*>(args[0]));
                return std::bit_cast<uint64_t>(str);
            },
            .ArgTypes = { stringType },
            .ReturnType = stringType
        },
        {
            .Name = "str_retain",
            .MangledName = "str_retain",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(char*)>(NRuntime::str_retain)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::str_retain(reinterpret_cast<char*>(args[0]));
                return 0;
            },
            .ArgTypes = { stringType },
            .ReturnType = voidType
        },
        {
            .Name = "str_release",
            .MangledName = "str_release",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(char*)>(NRuntime::str_release)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::str_release(reinterpret_cast<char*>(args[0]));
                return 0;
            },
            .ArgTypes = { stringType },
            .ReturnType = voidType
        },
        {
            .Name = "str_concat",
            .MangledName = "str_concat",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(const char*, const char*)>(NRuntime::str_concat)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto* str = NRuntime::str_concat(reinterpret_cast<const char*>(args[0]), reinterpret_cast<const char*>(args[1]));
                return std::bit_cast<uint64_t>(str);
            },
            .ArgTypes = { stringType, stringType },
            .ReturnType = stringType
        },
        {
            .Name = "str_compare",
            .MangledName = "str_compare",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*, const char*)>(NRuntime::str_compare)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_compare(reinterpret_cast<const char*>(args[0]), reinterpret_cast<const char*>(args[1]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { stringType, stringType },
            .ReturnType = integerType
        },

    };

    for (const auto& fn : functions) {
        auto funType = std::make_shared<NAst::TFunctionType>(fn.ArgTypes, fn.ReturnType);
        std::vector<NAst::TParam> params;
        for (size_t i = 0; i < fn.ArgTypes.size(); ++i) {
            params.push_back(std::make_shared<NAst::TVarStmt>(TLocation{}, "arg" + std::to_string(i), fn.ArgTypes[i]));
        }
        auto funDecl = std::make_shared<NAst::TFunDecl>(TLocation{}, fn.Name, params, nullptr, fn.ReturnType);
        funDecl->MangledName = fn.MangledName;
        funDecl->Type = funType;
        funDecl->Ptr = fn.Ptr;
        funDecl->Packed = fn.Packed;
        ctx.DeclareFunction(fn.Name, funDecl);
    }
}

} // namespace NRegistry
} // namespace NQumir