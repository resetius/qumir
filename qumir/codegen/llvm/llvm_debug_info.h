#pragma once

#include <qumir/ir/builder.h>

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugLoc.h>

#include <span>
#include <unordered_map>

namespace llvm {
class AllocaInst;
} // namespace llvm

namespace NQumir {
namespace NCodeGen {

class TLLVMDebugInfo {
public:
    TLLVMDebugInfo(llvm::Module& module, const NIR::TModule& sourceModule);

    void BeginFunction(const NIR::TFunction& function, llvm::Function& llvmFunction);
    void DeclareLocals(std::span<llvm::AllocaInst* const> allocas, llvm::BasicBlock& entryBlock);
    llvm::DebugLoc GetLocation(const NIR::TInstrDebugInfo& info);
    void Finalize();

private:
    llvm::DIType* GetType(const NAst::TTypePtr& astType, int typeId);
    llvm::DILocalScope* GetScope(int scopeId);

    llvm::Module& Module_;
    const NIR::TModule& SourceModule_;
    llvm::DIBuilder Builder_;
    llvm::DIFile* File_;
    const NIR::TFunction* Function_ = nullptr;
    llvm::DISubprogram* Subprogram_ = nullptr;
    std::unordered_map<int, llvm::DILocalScope*> Scopes_;
    std::unordered_map<int, std::unordered_map<const NAst::TType*, llvm::DIType*>> Types_;
};

} // namespace NCodeGen
} // namespace NQumir
