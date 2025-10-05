#pragma once

#include <qumir/ir/builder.h>
#include <qumir/ir/vminstr.h>

namespace NQumir {
namespace NIR {

struct TExecFunc {
    int UniqueId;
    std::vector<TInstr> Code;
    std::vector<TVMInstr> VMCode;
    int32_t MaxTmpIdx{0};
};

class TVMCompiler {
public:
    TVMCompiler(TModule& module)
        : Module(module)
    {}

    TExecFunc& Compile(const TFunction& function);

private:
    void CompileUltraLow(const TFunction& function, TExecFunc& out);

    TModule& Module;
    std::unordered_map<int, TExecFunc> CodeCache;
};

} // namespace NIR
} // namespace NQumir
