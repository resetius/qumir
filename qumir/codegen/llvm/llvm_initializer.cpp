#include "llvm_initializer.h"

#include <llvm/Support/TargetSelect.h>

namespace NQumir::NCodeGen {

TLLVMInitializer::TLLVMInitializer() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
}

TLLVMInitializer::~TLLVMInitializer() {

}

} // namespace NOz::NCodeGen