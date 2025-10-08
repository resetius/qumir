#pragma once

// Lightweight forward declarations to avoid heavy LLVM includes in dependents.
#include <memory>
#include <ostream>
#include <string>
#include <vector>
#include <unordered_map>

namespace llvm {
class LLVMContext;
class Module;
class Function;
class Type;
class Value;
class BasicBlock;
class IRBuilderBase;
class TargetMachine;
class GlobalVariable;
} // namespace llvm

namespace NQumir::NIR {
struct TModule;
struct TFunction;
struct TInstr;
struct TBlock;
} // namespace NQumir::NIR

namespace NQumir::NCodeGen {

struct TLLVMCodeGenOptions {
    std::string ModuleName {"oz_module"};
    bool Optimize {false};
};

struct ILLVMModuleArtifacts {
    virtual ~ILLVMModuleArtifacts() = default;
    // Expose defined (non-declaration) function names for tests/introspection without LLVM headers
    virtual const std::vector<std::string>& GetDefinedFunctionNames() const = 0;
    virtual void PrintModule(std::ostream& os) const = 0;
    virtual void Generate(std::ostream& os, bool generateAsm, bool generateObj) const = 0;
};

class TLLVMCodeGen {
public:
    explicit TLLVMCodeGen(const TLLVMCodeGenOptions& opts = {});
    ~TLLVMCodeGen();

    std::unique_ptr<ILLVMModuleArtifacts> Emit(const NIR::TModule& module, int optLevel = 0);

    void PrintFunction(int symId, std::ostream& os) const;

private:
    llvm::Function* LowerFunction(const NIR::TFunction& fun, const NIR::TModule& module);
    void LowerBlock(const NIR::TBlock& blk, const NIR::TModule& module, llvm::Function* lf, std::vector<llvm::BasicBlock*>& orderedBBs);
    llvm::Value* LowerInstr(const NIR::TInstr& instr, const NIR::TModule& module);
    void CreateTargetMachine();
    void Optimize(int optLevel);

    llvm::GlobalVariable* EnsureSlotGlobal(int64_t sidx, const NIR::TModule& module);

private:
    TLLVMCodeGenOptions Opts;
    std::unique_ptr<llvm::LLVMContext> Ctx;
    std::unique_ptr<llvm::Module> LModule;
    std::unique_ptr<llvm::TargetMachine> TM;
    std::unique_ptr<llvm::IRBuilderBase> BuilderBase; // concrete created in cpp

    struct TFunState {
        const NIR::TFunction* Fun {nullptr};
        llvm::Function* LFun {nullptr};
        std::vector<llvm::Value*> TmpValues; // size = Fun->NextTmpIdx
        std::unordered_map<int64_t, llvm::BasicBlock*> LabelToBB; // direct mapping for jmp/cmp targets
        std::vector<llvm::Value*> PendingArgs; // collected via 'arg' ops before 'call'
    };
    std::unique_ptr<TFunState> CurFun;
    // Module-global slot storage (i64 globals), indexed by module-wide slot index
    std::vector<llvm::Value*> ModuleSlots;
    // Map from SymId to lowered LLVM function for call lowering
    std::unordered_map<int, llvm::Function*> SymIdToLFun;
    // Map from SymId to parameter slot indices (module-global slot ids for parameters)
    std::unordered_map<int, std::vector<int64_t>> SymIdToParamSlots;
    std::unordered_map<int, int> SymIdToUniqueFunId; // for updating code of updated functions
};

} // namespace NQumir::NCodeGen
