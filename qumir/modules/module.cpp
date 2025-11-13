#include "module.h"

namespace NQumir {
namespace NRegistry {

void IModule::Register(NSemantics::TNameResolver& ctx) {
    ctx.RegisterModule(this);
    ctx.ImportModule(Name());
}

} // namespace NRegistry
} // namespace NQumir