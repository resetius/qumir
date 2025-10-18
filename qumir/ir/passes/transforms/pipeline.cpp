#include "pipeline.h"

#include <qumir/ir/passes/transforms/locals2ssa.h>
#include <qumir/ir/passes/transforms/de_ssa.h>
#include <qumir/ir/passes/transforms/renumber_regs.h>

namespace NQumir {
namespace NIR {
namespace NPasses {

void Pipeline(TFunction& function, TModule& module) {
    PromoteLocalsToSSA(function, module);
    DeSSA(function, module);
    RenumberRegisters(function, module);
}

void Pipeline(TModule& module) {
    for (auto& function : module.Functions) {
        Pipeline(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir