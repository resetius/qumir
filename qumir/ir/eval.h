#pragma once

#include "builder.h"
#include "vmcompiler.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace NQumir {
namespace NIR {

struct TRuntime {
    std::vector<int64_t> Globals;
    std::vector<int64_t> Stack;
    std::vector<int64_t> Args; // call arguments, will be copied on stack on call, TODO: remove
};

struct TExecFunc;

struct TFrame {
    TExecFunc* Exec{nullptr};
    std::vector<int64_t> Tmps;
    uint64_t StackBase = 0;
    TVMInstr* PC{nullptr};
    uint8_t LastCmp{0}; // 1 if the last cmp branched to the true edge, 0 otherwise
};

// Link to caller frame for returning
struct TReturnLink {
    int64_t FrameIdx;
    int32_t CallerDst; // destination tmp idx in caller frame, -1 if none
};

class TInterpreter {
public:
    TInterpreter(TModule& module, std::ostream& out, std::istream& in);

    std::optional<std::string> Eval(TFunction& function, std::vector<int64_t> args);

private:
    //TExecFunc Compile(TFunction& function);

    std::ostream& Out;
    std::istream& In;
    TModule& Module;
    TRuntime Runtime;
    TVMCompiler Compiler;
    std::vector<TReturnLink> ReturnLinks;
};

} // namespace NIR
} // namespace NQumir