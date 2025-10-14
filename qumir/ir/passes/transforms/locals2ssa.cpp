#include "locals2ssa.h"

namespace NQumir {
namespace NIR {
namespace NPasses {

void PromoteLocalsToSSA(TFunction& function, TModule& module)
{

}

void PromoteLocalsToSSA(TModule& module) {
    for (auto& function : module.Functions) {
        PromoteLocalsToSSA(function, module);
    }
}

} // namespace NPasses
} // namespace NIR
} // namespace NQumir