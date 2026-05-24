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
    std::vector<int64_t> Regs;
    std::vector<int64_t> SavedRegs;
};

struct TExecFunc;

struct TFrame {
    const TExecFunc* Exec{nullptr};
    const int UsedRegs = 0;
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

private:
    std::optional<std::string> DoEval(TFunction& function, std::vector<int64_t> args, TOptions options);

    TFuture<std::optional<std::string>> DoEvalAsync(TFunction& function, std::vector<int64_t> args, TOptions options);
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
