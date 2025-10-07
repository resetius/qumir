#include "system.h"

#include <iostream>

#include <math.h>

namespace NQumir {
namespace NRegistry {

namespace {

struct TExternalFunction {
    std::string Name;
    std::string MangledName;
    void* Ptr;
    std::vector<NAst::TTypePtr> ArgTypes;
    NAst::TTypePtr ReturnType;
};

}

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
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "iabs",
            .MangledName = "abs",
            .Ptr = reinterpret_cast<void*>(static_cast<int(*)(int)>(abs)),
            .ArgTypes = { integerType },
            .ReturnType = integerType,
        },
        {
            .Name = "abs",
            .MangledName = "fabs",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(fabs)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "sin",
            .MangledName = "sin",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(sin)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "cos",
            .MangledName = "cos",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(cos)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "tg",
            .MangledName = "tan",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(tan)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "ctg",
            .MangledName = "cotan",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(cotan)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "ln",
            .MangledName = "log",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(log)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "lg",
            .MangledName = "log10",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(log10)),
            .ArgTypes = { floatType },
            .ReturnType = floatType,
        },
        {
            .Name = "exp",
            .MangledName = "exp",
            .Ptr = reinterpret_cast<void*>(static_cast<double(*)(double)>(exp)),
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
        ctx.DeclareFunction(fn.Name, funDecl);
    }
}

} // namespace NRegistry
} // namespace NQumir