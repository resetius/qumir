#include "llvm_codegen_impl.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Config/llvm-config.h>
#include <stdexcept>

#include <llvm/TargetParser/Host.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Support/FileSystem.h>

namespace NQumir::NCodeGen {

namespace {

constexpr auto AssemblyFile = llvm::CodeGenFileType::AssemblyFile;
constexpr auto ObjectFile = llvm::CodeGenFileType::ObjectFile;

} // namespace

void TLLVMModuleArtifacts::Generate(std::ostream& os, bool generateAsm, bool generateObj) const {
    std::string triple = Module->getTargetTriple();
    if (triple.empty()) {
        triple = llvm::sys::getDefaultTargetTriple();
        Module->setTargetTriple(triple);
    }

    std::string errStr;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple, errStr);
    if (!target) {
        throw std::runtime_error(std::string("lookupTarget failed: ") + errStr);
    }

    llvm::TargetOptions opt;
    auto RM = std::optional<llvm::Reloc::Model>(llvm::Reloc::PIC_);
    std::unique_ptr<llvm::TargetMachine> TM {
        target->createTargetMachine(triple, "generic", "", opt, RM)
    };
    if (!TM) {
        throw std::runtime_error("createTargetMachine failed");
    }
    Module->setDataLayout(TM->createDataLayout());

    llvm::legacy::PassManager pm;
    llvm::SmallVector<char, 0> buf;
    llvm::raw_svector_ostream rso(buf);

    if (generateAsm) {
        if (TM->addPassesToEmitFile(pm, rso, nullptr, AssemblyFile))
        {
            throw std::runtime_error("TargetMachine can't emit assembly");
        }
    } else if (generateObj) {
        if (TM->addPassesToEmitFile(pm, rso, nullptr, ObjectFile))
        {
            throw std::runtime_error("TargetMachine can't emit object code");
        }
    } else {
        throw std::runtime_error("Generate: neither assembly nor object requested");
    }
    pm.run(*Module);

    os.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    os.flush();
}

} // namespace NQumir::NCodeGen