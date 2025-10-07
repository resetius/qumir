#include "system.h"

#include <iostream>

#include <stdlib.h>
#include <math.h>

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

extern "C" double cotan(double x) {
    return 1.0 / tan(x);
}

void SystemModule::Register(NSemantics::TNameResolver& ctx) {
    auto integerType = std::make_shared<NAst::TIntegerType>();
    auto floatType = std::make_shared<NAst::TFloatType>();

    std::vector<TExternalFunction> functions = {
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
        }
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