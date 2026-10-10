#pragma once

#include <qumir/ir/builder.h>
#include <qumir/ir/vminstr.h>
#include <qumir/ir/ffi.h>

#include <memory>

namespace NQumir {
namespace NIR {

struct TExecFunc {
    int UniqueId;
    std::vector<TVMInstr> VMCode;
    int32_t RegisterFileSize = 0;
    int32_t RegisterFileAlignment = 8;
    int32_t NumLocals{0};        // frame size in bytes (not variable count)
    std::vector<int> ArgByteOffsets; // byte offset of each argument local in the frame (for func args only!)
    std::vector<int> ArgTypeIds;     // IR typeId of each argument (eval uses SizeInBytes to handle struct)
    std::unordered_map<int, int> TmpByteOffsets; // IR tmp id -> register file byte offset
    struct TStructRegister {
        int FrameOffset;
        int Size;
    };
    std::unordered_map<int, TStructRegister> StructRegisters; // register byte offset -> frame storage

    // debug
    std::vector<int> LocalByteOffsets; // byte offset of each local in the frame (for debug only)
    std::vector<TInstrDebugInfo> InstrDebugInfo;
};

class TVMCompiler {
public:
    TVMCompiler(TModule& module)
        : Module(module)
    {}

    TExecFunc& Compile(TFunction& function, bool printByteCode = false);

private:
    void CompileUltraLow(const TFunction& function, TExecFunc& out);
    bool CompileVectorInstruction(const TFunction& function, const TInstr& instr, TVMInstr& out);
    void AllocateRegisters(
        const TFunction& function,
        const std::vector<int>& tmpFrameOffsets,
        TExecFunc& out);

    // nullptr if the symbol is missing or the signature is unsupported.
    NFFI::IFunction* GetOrCreateExternalThunk(int externIdx);

    TModule& Module;
    std::unordered_map<int, TExecFunc> CodeCache;
    std::vector<std::unique_ptr<NFFI::IFunction>> ExternalThunks;
    std::unordered_map<int, NFFI::IFunction*> ExternalThunkCache;
};

} // namespace NIR
} // namespace NQumir
