#include "llvm_initializer.h"

#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>

namespace NQumir::NCodeGen {

TLLVMInitializer::TLLVMInitializer() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    LLVMInitializeWebAssemblyTargetInfo();
    LLVMInitializeWebAssemblyTarget();
    LLVMInitializeWebAssemblyTargetMC();
    LLVMInitializeWebAssemblyAsmPrinter();
    LLVMInitializeWebAssemblyAsmParser();
    LLVMInitializeWebAssemblyDisassembler();
}

TLLVMInitializer::~TLLVMInitializer() {

}

} // namespace NOz::NCodeGen