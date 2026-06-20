#pragma once

#include <qumir/parser/type.h>

namespace NQumir {
namespace NSemantics {

enum class ELifetimeKind {
    Trivial,
    Unique,
    RefCounted,
    Aggregate,
};

struct TLifetimeTraits {
    ELifetimeKind Kind;
    bool CanCopy;
    bool NeedsDestroy;
};

TLifetimeTraits GetLifetimeTraits(const NAst::TTypePtr& type);

} // namespace NSemantics
} // namespace NQumir
