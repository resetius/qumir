#pragma once

#include <qumir/ir/builder.h>
#include <qumir/ir/vminstr.h>
#include <qumir/ir/ffi.h>

#include <memory>

namespace NQumir {
namespace NIR {

struct TExecFunc {
    int UniqueId;
    std::vector<TInstr> Code;
    std::vector<TVMInstr> VMCode;
    int32_t MaxTmpIdx{0};
    int32_t NumLocals{0};        // frame size in bytes (not variable count)
    std::vector<int> ArgByteOffsets; // byte offset of each argument local in the frame
    std::vector<int> ArgTypeIds;     // IR typeId of each argument (eval uses SizeInBytes to handle struct)
    std::vector<int> TmpTypeIds;     // IR typeId of each tmp (eval uses it for VM-only packed ABI)
    std::vector<int> TmpFrameOffsets; // optional frame storage for address-backed tmp values
};

class TVMCompiler {
public:
    TVMCompiler(TModule& module)
        : Module(module)
    {}

    TExecFunc& Compile(TFunction& function, bool printByteCode = false);

private:
    void CompileUltraLow(const TFunction& function, TExecFunc& out);

    // nullptr if the symbol is missing or the signature is unsupported.
    NFFI::IFunction* GetOrCreateExternalThunk(int externIdx);

    TModule& Module;
    std::unordered_map<int, TExecFunc> CodeCache;
    std::vector<std::unique_ptr<NFFI::IFunction>> ExternalThunks;
    std::unordered_map<int, NFFI::IFunction*> ExternalThunkCache;
};

} // namespace NIR
} // namespace NQumir
