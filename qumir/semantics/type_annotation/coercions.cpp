#include "coercions.h"

#include <qumir/semantics/name_resolution/name_resolver.h>

namespace NQumir {
namespace NTypeAnnotation {

using namespace NAst;

namespace {

bool SameKind(const TTypePtr& a, const TTypePtr& b) {
    if (a->TypeName() != b->TypeName()) {
        return false;
    }
    if (auto ai = TMaybeType<TIntegerType>(a)) {
        auto bi = TMaybeType<TIntegerType>(b);
        return bi && ai.Cast()->Kind == bi.Cast()->Kind;
    }
    if (auto an = TMaybeType<TNamedType>(a)) {
        auto bn = TMaybeType<TNamedType>(b);
        return bn && an.Cast()->Name == bn.Cast()->Name;
    }
    return true;
}

// Cost of matching a generic type parameter against
// any concrete argument type. Deliberately higher than any real coercion cost
// (the highest of which is 2, for int->float) so that overload resolution
// always prefers a concrete overload over a generic one, falling back to the
// generic only when nothing concrete fits — see AnnotateOverloadedCall.
constexpr int GenericParamCost = 1'000'000;

bool IsGenericTypeParam(const TTypePtr& type, const std::unordered_set<std::string>* genericTypeParams) {
    auto named = TMaybeType<TNamedType>(type);
    return genericTypeParams && named && genericTypeParams->contains(named.Cast()->Name);
}

bool WideningInt(const TTypePtr& from, const TTypePtr& to) {
    auto s = TMaybeType<TIntegerType>(from).Cast();
    auto d = TMaybeType<TIntegerType>(to).Cast();
    if (!s || !d) {
        return false;
    }
    int ws = s->BitWidth(), wd = d->BitWidth();
    bool ss = s->IsSigned(), sd = d->IsSigned();
    if (ss == sd) {
        return wd >= ws;
    }
    if (!ss && sd) {
        return wd > ws; // u8->i16, u16->i32, u32->i64
    }
    return false;
}

} // namespace

std::optional<int> ArgCost(
    const TTypePtr& from,
    const TTypePtr& to,
    NSemantics::TNameResolver* ctx,
    const std::unordered_set<std::string>* genericTypeParams)
{
    if (!from || !to) {
        return std::nullopt;
    }

    // Generic type parameter: matches any concrete argument type, but at a
    // deliberately high fixed cost (see GenericParamCost) — the actual type
    // bound to the parameter, and the function instantiation for it, are
    // determined once this overload is chosen as the best match.
    if (IsGenericTypeParam(to, genericTypeParams)) {
        return GenericParamCost;
    }

    if (SameKind(from, to)) {
        return 0;
    }

    if (TMaybeType<TIntegerType>(from) && TMaybeType<TIntegerType>(to)) {
        return WideningInt(from, to) ? std::optional<int>{1} : std::nullopt;
    }

    if (TMaybeType<TIntegerType>(from) && TMaybeType<TFloatType>(to)) {
        return 2;
    }
    if (TMaybeType<TFloatType>(from) && TMaybeType<TFloatType>(to)) {
        return 1;
    }

    if (ctx && ctx->GetCast(from, to)) {
        return 1;
    }

    return std::nullopt;
}

} // namespace NTypeAnnotation
} // namespace NQumir
