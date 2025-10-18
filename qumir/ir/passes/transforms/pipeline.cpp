#include "pipeline.h"

#include <qumir/ir/passes/transforms/locals2ssa.h>
#include <qumir/ir/passes/transforms/de_ssa.h>
#include <qumir/ir/passes/transforms/renumber_regs.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

using namespace NLiterals;

void Pipeline(TFunction& function, TModule& module) {
    PromoteLocalsToSSA(function, module);
    RenumberRegisters(function, module);
    // remove nops
    for (auto& block : function.Blocks) {
        block.Instrs.erase(
            std::remove_if(
                block.Instrs.begin(),
                block.Instrs.end(),
                [](const TInstr& instr) {
                    return instr.Op == "nop"_op;
                }
            ),
            block.Instrs.end()
        );
    }
}

void Pipeline(TModule& module) {
    for (auto& function : module.Functions) {
        Pipeline(function, module);
    }
}

void BeforeCompile(TFunction& function, TModule& module) {
    DeSSA(function, module);
}

void BeforeCompile(TModule& module) {
    for (auto& function : module.Functions) {
        BeforeCompile(function, module);
    }
}


} // namespace NPasses
} // namespace NIR
} // namespace NQumir