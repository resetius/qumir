#pragma once

#include "builder.h"
#include "vmcompiler.h"

#include <cstdint>
#include <ostream>
#include <vector>

namespace NQumir {
namespace NIR {

struct TRuntime {
    std::vector<int64_t> Slots;
    std::vector<uint8_t> Inited;
};

struct TExecFunc;

struct TFrame {
    TExecFunc* Exec{nullptr};
    std::vector<int64_t> Tmps;
    std::vector<int64_t> Locals; // TODO: Create unified stack for whole run
    std::vector<int64_t> Args; // call arguments
    TVMInstr* PC{nullptr};
    uint8_t LastCmp{0}; // 1 if the last cmp branched to the true edge, 0 otherwise
};

// For saving/restoring parameters across calls
struct TParamSave {
    int64_t Sid;
    int64_t Old;
    uint8_t OldInit;
};

// Link to caller frame for returning
struct TReturnLink {
    int64_t FrameIdx;
    int32_t CallerDst; // destination tmp idx in caller frame, -1 if none
    std::vector<TParamSave> Saved;
};

class TInterpreter {
public:
    TInterpreter(TModule& module, TRuntime& runtime, std::ostream& out, std::istream& in);

    std::optional<std::string> Eval(TFunction& function, std::vector<int64_t> args);

private:
    //TExecFunc Compile(TFunction& function);

    std::ostream& Out;
    std::istream& In;
    TModule& Module;
    TRuntime& Runtime;
    TVMCompiler Compiler;
    std::vector<TReturnLink> ReturnLinks;
};

} // namespace NIR
} // namespace NQumir