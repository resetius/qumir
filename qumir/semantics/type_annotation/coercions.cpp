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
        return bn && TypeKey(a) == TypeKey(b);
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

std::optional<int> GenericParametricCost(
    const TTypePtr& paramType,
    const TTypePtr& argType,
    const std::unordered_set<std::string>* genericTypeParams);

std::optional<int> CombineGenericCost(
    const TTypePtr& paramType,
    const TTypePtr& argType,
    const std::unordered_set<std::string>* genericTypeParams,
    bool& matched)
{
    if (auto cost = GenericParametricCost(paramType, argType, genericTypeParams)) {
        matched = true;
        return cost;
    }
    if (TypeKey(paramType) == TypeKey(argType)) {
        return 0;
    }
    return std::nullopt;
}

std::optional<int> GenericParametricCost(
    const TTypePtr& paramType,
    const TTypePtr& argType,
    const std::unordered_set<std::string>* genericTypeParams)
{
    if (!genericTypeParams || genericTypeParams->empty() || !paramType || !argType) {
        return std::nullopt;
    }
    if (IsGenericTypeParam(paramType, genericTypeParams)) {
        return GenericParamCost;
    }
    bool matched = false;
    int total = 0;
    if (auto paramNamed = TMaybeType<TNamedType>(paramType)) {
        auto argNamed = TMaybeType<TNamedType>(argType);
        if (!argNamed || paramNamed.Cast()->Name != argNamed.Cast()->Name
            || paramNamed.Cast()->TypeArgs.size() != argNamed.Cast()->TypeArgs.size())
        {
            return std::nullopt;
        }
        for (size_t i = 0; i < paramNamed.Cast()->TypeArgs.size(); ++i) {
            const auto& paramArg = paramNamed.Cast()->TypeArgs[i];
            const auto& arg = argNamed.Cast()->TypeArgs[i];
            if (paramArg.Kind != TGenericArg::EKind::Type || arg.Kind != TGenericArg::EKind::Type) {
                return std::nullopt;
            }
            auto cost = CombineGenericCost(paramArg.Type, arg.Type, genericTypeParams, matched);
            if (!cost) {
                return std::nullopt;
            }
            total += *cost;
        }
        return matched ? std::optional<int>{total} : std::nullopt;
    }
    if (auto paramArray = TMaybeType<TArrayType>(paramType)) {
        auto argArray = TMaybeType<TArrayType>(argType);
        if (!argArray || paramArray.Cast()->Arity != argArray.Cast()->Arity) {
            return std::nullopt;
        }
        auto cost = CombineGenericCost(paramArray.Cast()->ElementType, argArray.Cast()->ElementType, genericTypeParams, matched);
        return cost && matched ? cost : std::nullopt;
    }
    if (auto paramPtr = TMaybeType<TPointerType>(paramType)) {
        auto argPtr = TMaybeType<TPointerType>(argType);
        if (!argPtr) {
            return std::nullopt;
        }
        auto cost = CombineGenericCost(paramPtr.Cast()->PointeeType, argPtr.Cast()->PointeeType, genericTypeParams, matched);
        return cost && matched ? cost : std::nullopt;
    }
    if (auto paramRef = TMaybeType<TReferenceType>(paramType)) {
        auto argRef = TMaybeType<TReferenceType>(argType);
        auto argInner = argRef ? argRef.Cast()->ReferencedType : argType;
        auto cost = CombineGenericCost(paramRef.Cast()->ReferencedType, argInner, genericTypeParams, matched);
        return cost && matched ? cost : std::nullopt;
    }
    if (auto paramFuture = TMaybeType<TFutureType>(paramType)) {
        auto argFuture = TMaybeType<TFutureType>(argType);
        if (!argFuture) {
            return std::nullopt;
        }
        auto cost = CombineGenericCost(paramFuture.Cast()->ResultType, argFuture.Cast()->ResultType, genericTypeParams, matched);
        return cost && matched ? cost : std::nullopt;
    }
    if (auto paramFun = TMaybeType<TFunctionType>(paramType)) {
        auto argFun = TMaybeType<TFunctionType>(argType);
        if (!argFun || paramFun.Cast()->ParamTypes.size() != argFun.Cast()->ParamTypes.size()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < paramFun.Cast()->ParamTypes.size(); ++i) {
            auto cost = CombineGenericCost(paramFun.Cast()->ParamTypes[i], argFun.Cast()->ParamTypes[i], genericTypeParams, matched);
            if (!cost) {
                return std::nullopt;
            }
            total += *cost;
        }
        auto retCost = CombineGenericCost(paramFun.Cast()->ReturnType, argFun.Cast()->ReturnType, genericTypeParams, matched);
        if (!retCost) {
            return std::nullopt;
        }
        total += *retCost;
        return matched ? std::optional<int>{total} : std::nullopt;
    }
    if (auto paramStruct = TMaybeType<TStructType>(paramType)) {
        auto argStruct = TMaybeType<TStructType>(argType);
        if (!argStruct || paramStruct.Cast()->Fields.size() != argStruct.Cast()->Fields.size()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < paramStruct.Cast()->Fields.size(); ++i) {
            if (paramStruct.Cast()->Fields[i].first != argStruct.Cast()->Fields[i].first) {
                return std::nullopt;
            }
            auto cost = CombineGenericCost(paramStruct.Cast()->Fields[i].second, argStruct.Cast()->Fields[i].second, genericTypeParams, matched);
            if (!cost) {
                return std::nullopt;
            }
            total += *cost;
        }
        return matched ? std::optional<int>{total} : std::nullopt;
    }
    return std::nullopt;
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
    if (auto cost = GenericParametricCost(to, from, genericTypeParams)) {
        return cost;
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
