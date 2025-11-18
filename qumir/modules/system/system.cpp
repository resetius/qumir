#include "system.h"

#include <iostream>

#include <stdlib.h>
#include <math.h>

#include <qumir/runtime/runtime.h>

namespace NQumir {
namespace NRegistry {

SystemModule::SystemModule() {
    auto integerType = std::make_shared<NAst::TIntegerType>();
    auto floatType = std::make_shared<NAst::TFloatType>();
    auto boolType = std::make_shared<NAst::TBoolType>();
    auto voidType = std::make_shared<NAst::TVoidType>();
    auto stringType = std::make_shared<NAst::TStringType>();
    auto voidPtrType = std::make_shared<NAst::TPointerType>(voidType);
    auto fileType = std::make_shared<NAst::TFileType>();

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
            .Name = "лит_в_вещ",
            .MangledName = "atof",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(const char*)>(atof)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(atof(reinterpret_cast<const char*>(args[0])));
            },
            .ArgTypes = { stringType },
            .ReturnType = floatType,
        },
        {
            .Name = "лит_в_цел",
            .MangledName = "atoll",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*)>(atoll)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(atoll(reinterpret_cast<const char*>(args[0])));
            },
            .ArgTypes = { stringType },
            .ReturnType = integerType,
        },
        {
            .Name = "вещ_в_лит",
            .MangledName = "str_from_double",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(double)>(NRuntime::str_from_double)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(NRuntime::str_from_double(std::bit_cast<double>(args[0])));
            },
            .ArgTypes = { floatType },
            .ReturnType = stringType,
        },
        {
            .Name = "цел_в_лит",
            .MangledName = "str_from_int",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(int64_t)>(NRuntime::str_from_int)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(NRuntime::str_from_int(std::bit_cast<int64_t>(args[0])));
            },
            .ArgTypes = { integerType },
            .ReturnType = stringType,
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
            .MangledName = "rand_int64",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t)>(rand_int64)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_int64(std::bit_cast<int64_t>(args[0])));
            },
            .ArgTypes = { integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "irand",
            .MangledName = "rand_int64_range",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, int64_t)>(rand_int64_range)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return std::bit_cast<uint64_t>(rand_int64_range(std::bit_cast<int64_t>(args[0]), std::bit_cast<int64_t>(args[1])));
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
        {
            .Name = "output_bool",
            .MangledName = "output_bool",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int64_t)>(NRuntime::output_bool)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::output_bool(std::bit_cast<int64_t>(args[0]));
                return 0;
            },
            .ArgTypes = { boolType },
            .ReturnType = voidType,
        },
        {
            .Name = "output_symbol",
            .MangledName = "output_symbol",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int32_t)>(NRuntime::output_symbol)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::output_symbol(static_cast<int32_t>(std::bit_cast<int64_t>(args[0])));
                return 0;
            },
            .ArgTypes = { std::make_shared<NAst::TSymbolType>() },
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
            .Name = "str_slice",
            .MangledName = "str_slice",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(const char*, int, int)>(NRuntime::str_slice)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto* str = NRuntime::str_slice(reinterpret_cast<const char*>(args[0]), static_cast<int>(std::bit_cast<int64_t>(args[1])), static_cast<int>(std::bit_cast<int64_t>(args[2])));
                return std::bit_cast<uint64_t>(str);
            },
            .ArgTypes = { stringType, integerType, integerType },
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
        {
            .Name = "длин",
            .MangledName = "str_len",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*)>(NRuntime::str_len)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_len(reinterpret_cast<const char*>(args[0]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { stringType },
            .ReturnType = integerType,
            .RequireArgsMaterialization = true
        },
        {
            .Name = "юникод",
            .MangledName = "str_unicode",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*)>(NRuntime::str_unicode)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_unicode(reinterpret_cast<const char*>(args[0]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { stringType },
            .ReturnType = integerType,
        },
        {
            .Name = "юнисимвол",
            .MangledName = "str_from_unicode",
            .Ptr = reinterpret_cast<void*>(static_cast<char*(*)(int64_t)>(NRuntime::str_from_unicode)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto* str = NRuntime::str_from_unicode(std::bit_cast<int64_t>(args[0]));
                return std::bit_cast<uint64_t>(str);
            },
            .ArgTypes = { integerType },
            .ReturnType = stringType,
        },
        {
            .Name = "позиция",
            .MangledName = "str_str",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*, const char*)>(NRuntime::str_str)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_str(reinterpret_cast<const char*>(args[0]), reinterpret_cast<const char*>(args[1]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { stringType, stringType },
            .ReturnType = integerType,
        },
        {
            .Name = "поз", // alias for позиция
            .MangledName = "str_str",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(const char*, const char*)>(NRuntime::str_str)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_str(reinterpret_cast<const char*>(args[0]), reinterpret_cast<const char*>(args[1]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { stringType, stringType },
            .ReturnType = integerType,
        },
        {
            .Name = "позиция после",
            .MangledName = "str_str_from",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, const char*, const char*)>(NRuntime::str_str_from)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_str_from(std::bit_cast<int64_t>(args[0]), reinterpret_cast<const char*>(args[1]), reinterpret_cast<const char*>(args[2]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { integerType, stringType, stringType },
            .ReturnType = integerType,
        },
        {
            .Name = "поз после", // alias for позиция после
            .MangledName = "str_str_from",
            .Ptr = reinterpret_cast<void*>(static_cast<int64_t(*)(int64_t, const char*, const char*)>(NRuntime::str_str_from)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::str_str_from(std::bit_cast<int64_t>(args[0]), reinterpret_cast<const char*>(args[1]), reinterpret_cast<const char*>(args[2]));
                return std::bit_cast<uint64_t>(ret);
            },
            .ArgTypes = { integerType, stringType, stringType },
            .ReturnType = integerType,
        },
        // arrays
        {
            .Name = "array_create",
            .MangledName = "array_create",
            .Ptr = reinterpret_cast<void*>(static_cast<void*(*)(size_t)>(NRuntime::array_create)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto* arr = NRuntime::array_create(static_cast<size_t>(args[0]));
                return std::bit_cast<uint64_t>(arr);
            },
            .ArgTypes = { integerType },
            .ReturnType = voidPtrType
        },
        {
            .Name = "array_destroy",
            .MangledName = "array_destroy",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(void*)>(NRuntime::array_destroy)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::array_destroy(reinterpret_cast<void*>(args[0]));
                return 0;
            },
            .ArgTypes = { voidPtrType },
            .ReturnType = voidType
        },
        {
            .Name = "array_str_destroy",
            .MangledName = "array_str_destroy",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(void*, size_t)>(NRuntime::array_str_destroy)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::array_str_destroy(reinterpret_cast<void*>(args[0]), static_cast<size_t>(args[1]));
                return 0;
            },
            .ArgTypes = { voidPtrType, integerType },
            .ReturnType = voidType
        },

        // files
        {
            .Name = "открыть на чтение",
            .MangledName = "file_open_for_read",
            .Ptr = reinterpret_cast<void*>(static_cast<int32_t(*)(const char*)>(NRuntime::file_open_for_read)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                return static_cast<uint64_t>(NRuntime::file_open_for_read(reinterpret_cast<const char*>(args[0])));
            },
            .ArgTypes = { stringType },
            .ReturnType = fileType,
        },
        {
            .Name = "закрыть",
            .MangledName = "file_close",
            .Ptr = reinterpret_cast<void*>(static_cast<void(*)(int32_t)>(NRuntime::file_close)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                NRuntime::file_close(static_cast<int32_t>(args[0]));
                return 0;
            },
            .ArgTypes = { fileType },
            .ReturnType = voidType,
        },
        {
            .Name = "есть данные",
            .MangledName = "file_has_more_data",
            .Ptr = reinterpret_cast<void*>(static_cast<bool(*)(int32_t)>(NRuntime::file_has_more_data)),
            .Packed = +[](const uint64_t* args, size_t argCount) -> uint64_t {
                auto ret = NRuntime::file_has_more_data(static_cast<int32_t>(args[0]));
                return static_cast<uint64_t>(ret);
            },
            .ArgTypes = { fileType },
            .ReturnType = boolType,
        },
    };

    ExternalFunctions_.swap(functions);
}

} // namespace NRegistry
} // namespace NQumir