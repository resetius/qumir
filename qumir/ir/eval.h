#pragma once

#include "builder.h"
#include "vmcompiler.h"

#include <coroutine>
#include <cstdint>
#include <ostream>
#include <vector>

#include <qumir/future.h>

namespace NQumir {
namespace NIR {

struct TRuntime {
    std::vector<char> Globals; // byte array; each variable slot is 8 bytes (64-bit aligned)
    std::vector<char> Stack;   // byte array; each variable slot is 8 bytes (64-bit aligned)
    std::vector<int64_t> Args; // call arguments, will be copied on stack on call, TODO: remove
    std::vector<__int128_t> Args128; // parallel to Args, filled for 128-bit arguments only
    __int128_t Ret128Value = 0;
    std::vector<int64_t> Regs;
    std::vector<__int128_t> Regs128; // addressed by the same register index as Regs
    std::vector<int64_t> SavedRegs;
};

struct TExecFunc;

struct TFrame {
    const TExecFunc* Exec{nullptr};
    const int UsedRegs = 0;
    const int Used128Regs = 0;
    const uint64_t StackBase = 0;
    TVMInstr* PC{nullptr};
    std::string_view Name;
};

// Link to caller frame for returning
struct TReturnLink {
    int64_t FrameIdx;
    int32_t CallerDst; // destination tmp idx in caller frame, -1 if none
    bool CalleeIsCoroutine = false;
    bool CalleeReturnsVoid = false;
};

class TInterpreter {
public:
    TInterpreter(TModule& module, std::ostream& out, std::istream& in);

    struct TOptions {
        bool PrintByteCode = false;
    };

    std::optional<std::string> Eval(TFunction& function, std::vector<int64_t> args, TOptions options);
    // Returns the VM register bits directly. This is intended for scalar POD
    // results whose caller already knows the function's return type.
    std::optional<int64_t> EvalRaw(TFunction& function, std::vector<int64_t> args, TOptions options);

private:
    std::optional<int64_t> DoEvalRaw(TFunction& function, std::vector<int64_t> args, TOptions options);

    TFuture<std::optional<int64_t>> DoEvalRawAsync(TFunction& function, std::vector<int64_t> args, TOptions options);
    size_t ProcessAsyncRuntimeEvents();

    std::ostream& Out;
    std::istream& In;
    TModule& Module;
    TRuntime Runtime;
    TVMCompiler Compiler;
    std::vector<TReturnLink> ReturnLinks;
};

} // namespace NIR
} // namespace NQumir
