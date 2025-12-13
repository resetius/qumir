#include "definite_assignment.h"

namespace NQumir {
namespace NSemantics {

using namespace NAst;

TDefiniteAssignmentChecker::TDefiniteAssignmentChecker(TNameResolver& context)
    : Context(context)
{
    for (const auto& global : Context.GetGlobals()) {
        GlobalAssigned.insert(global.Id);
    }
}

std::expected<void, TError> TDefiniteAssignmentChecker::Check(NAst::TExprPtr root) {
    TAssignedSet empty = GlobalAssigned;
    auto res = CheckExpr(root, TScopeId{0}, empty);
    if (!res) {
        return std::unexpected(res.error());
    }
    return {};
}

TDefiniteAssignmentChecker::TAssignedSet
TDefiniteAssignmentChecker::Intersect(const TAssignedSet& a, const TAssignedSet& b)
{
    TAssignedSet result;
    if (a.size() < b.size()) {
        for (int id : a) {
            if (b.contains(id)) {
                result.insert(id);
            }
        }
    } else {
        for (int id : b) {
            if (a.contains(id)) {
                result.insert(id);
            }
        }
    }
    return result;
}

TDefiniteAssignmentChecker::TAssignedSet
TDefiniteAssignmentChecker::Union(const TAssignedSet& a, const TAssignedSet& b)
{
    TAssignedSet result = a;
    for (int id : b) {
        result.insert(id);
    }
    return result;
}

std::expected<int, TError> TDefiniteAssignmentChecker::GetSymbolId(
    const std::string& name,
    TScopeId scopeId,
    const NAst::TExprPtr& where)
{
    auto symbolId = Context.Lookup(name, scopeId);
    if (!symbolId) {
        return std::unexpected(TError(where->Location,
            "Идентификатор '" + name + "' не определён."));
    }
    return symbolId->Id;
}

std::expected<NAst::TExprPtr, TError> TDefiniteAssignmentChecker::GetSymbolNode(
    const std::string& name,
    TScopeId scopeId,
    const NAst::TExprPtr& where)
{
    auto symbolId = Context.Lookup(name, scopeId);
    if (!symbolId) {
        return std::unexpected(TError(where->Location,
            "Идентификатор '" + name + "' не определён."));
    }
    return Context.GetSymbolNode(TSymbolId{symbolId->Id});
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckExpr(
    const NAst::TExprPtr& expr,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    if (!expr) {
        return inAssigned;
    }

    if (auto maybeBlock = TMaybeNode<TBlockExpr>(expr)) {
        return CheckBlock(maybeBlock.Cast(),
                          TScopeId{maybeBlock.Cast()->Scope},
                          inAssigned);
    }

    if (auto maybeIf = TMaybeNode<TIfExpr>(expr)) {
        return CheckIf(maybeIf.Cast(), scopeId, inAssigned);
    }

    if (auto maybeLoop = TMaybeNode<TLoopStmtExpr>(expr)) {
        return CheckLoop(maybeLoop.Cast(), scopeId, inAssigned);
    }

    if (auto maybeAssign = TMaybeNode<TAssignExpr>(expr)) {
        return CheckAssign(maybeAssign.Cast(), scopeId, inAssigned);
    }

    if (auto maybeVar = TMaybeNode<TVarStmt>(expr)) {
        return CheckVar(maybeVar.Cast(), scopeId, inAssigned);
    }

    if (auto maybeIdent = TMaybeNode<TIdentExpr>(expr)) {
        return CheckIdent(maybeIdent.Cast(), scopeId, inAssigned);
    }

    if (auto maybeFunDecl = TMaybeNode<TFunDecl>(expr)) {
        return CheckFunDecl(maybeFunDecl.Cast(), scopeId, inAssigned);
    }

    if (auto maybeIndex = TMaybeNode<TIndexExpr>(expr)) {
        if (auto maybeArrayType = TMaybeType<TArrayType>(maybeIndex.Cast()->Collection->Type)) {
            return inAssigned;
        }
    }
    if (auto multiIndex = TMaybeNode<TMultiIndexExpr>(expr)) {
        return inAssigned;
    }
    if (auto maybeCall = TMaybeNode<TCallExpr>(expr)) {
        return CheckCall(maybeCall.Cast(), scopeId, inAssigned);
    }

    TAssignedSet state = inAssigned;
    for (auto& childPtr : expr->Children()) {
        if (!childPtr) {
            continue;
        }
        auto childRes = CheckExpr(childPtr, scopeId, state);
        if (!childRes) {
            return std::unexpected(childRes.error());
        }
        state = std::move(*childRes);
    }
    return state;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError> TDefiniteAssignmentChecker::CheckCall(
    const std::shared_ptr<NAst::TCallExpr>& call,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    auto maybeIdent = TMaybeNode<TIdentExpr>(call->Callee);
    if (!maybeIdent) {
        return inAssigned;
    }

    auto maybeNode = GetSymbolNode(maybeIdent.Cast()->Name, scopeId, call->Callee);
    if (!maybeNode) {
        return std::unexpected(maybeNode.error());
    }

    auto result = inAssigned;
    std::list<TError> errors;
    auto& args = call->Args;
    if (auto fun = TMaybeNode<TFunDecl>(maybeNode.value())) {
        int i = 0;
        for (auto& param : fun.Cast()->Params) {
            if (auto maybeArrayType = TMaybeType<TArrayType>(param->Type)) {
                // skip array params
            } else if (auto maybeReferenceType = TMaybeType<TReferenceType>(param->Type)) {
                auto refType = maybeReferenceType.Cast();
                auto unwrappedType = refType->ReferencedType;
                if (unwrappedType->Readable) {
                    // inOut param
                    auto res = CheckExpr(args[i], scopeId, inAssigned);
                    if (!res) {
                        errors.push_back(res.error());
                    }
                } else {
                    // out param, add to assigned set
                    auto maybeIdentArg = TMaybeNode<TIdentExpr>(args[i]);
                    if (maybeIdentArg) {
                        auto symbolId = GetSymbolId(maybeIdentArg.Cast()->Name, scopeId, args[i]);
                        if (!symbolId) {
                            errors.push_back(symbolId.error());
                        } else {
                            result.insert(*symbolId);
                        }
                    }
                }
            } else {
                auto res = CheckExpr(args[i], scopeId, inAssigned);
                if (!res) {
                    errors.push_back(res.error());
                }
            }

            i++;
        }
    }


    if (!errors.empty()) {
        return std::unexpected(TError(call->Location, errors));
    }

    return result;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckBlock(
    const std::shared_ptr<TBlockExpr>& block,
    TScopeId /*scopeId*/,
    const TAssignedSet& inAssigned)
{
    TAssignedSet state = inAssigned;
    TScopeId blockScope{block->Scope};

    std::list<TError> errors;
    for (auto& stmt : block->Stmts) {
        auto res = CheckExpr(stmt, blockScope, state);
        if (!res) {
            errors.push_back(res.error());
        } else {
            state = Union(state, *res);
        }
    }
    if (!errors.empty()) {
        return std::unexpected(TError(block->Location, errors));
    }
    return state;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckIf(
    const std::shared_ptr<TIfExpr>& ifExpr,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    auto condRes = CheckExpr(ifExpr->Cond, scopeId, inAssigned);
    if (!condRes) {
        return std::unexpected(condRes.error());
    }
    TAssignedSet afterCond = std::move(*condRes);

    auto thenRes = CheckExpr(ifExpr->Then, scopeId, afterCond);
    if (!thenRes) {
        return std::unexpected(thenRes.error());
    }
    TAssignedSet thenState = std::move(*thenRes);

    if (ifExpr->Else) {
        auto elseRes = CheckExpr(ifExpr->Else, scopeId, afterCond);
        if (!elseRes) {
            return std::unexpected(elseRes.error());
        }
        TAssignedSet elseState = std::move(*elseRes);
        return Intersect(thenState, elseState);
    }

    return inAssigned;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckLoop(
    const std::shared_ptr<TLoopStmtExpr>& loop,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    TAssignedSet state = inAssigned;

    for (auto childPtr : loop->Children()) {
        if (!childPtr) {
            continue;
        }
        auto res = CheckExpr(childPtr, scopeId, state);
        if (!res) {
            return std::unexpected(res.error());
        }
        state = std::move(*res);
    }

    return inAssigned;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckAssign(
    const std::shared_ptr<TAssignExpr>& assign,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    auto valueRes = CheckExpr(assign->Value, scopeId, inAssigned);
    if (!valueRes) {
        return std::unexpected(valueRes.error());
    }

    TAssignedSet state = std::move(*valueRes);

    auto symbolId = GetSymbolId(assign->Name, scopeId, assign);
    if (!symbolId) {
        return std::unexpected(symbolId.error());
    }

    state.insert(*symbolId);
    return state;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckVar(
    const std::shared_ptr<TVarStmt>& var,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    return inAssigned;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckIdent(
    const std::shared_ptr<TIdentExpr>& ident,
    TScopeId scopeId,
    const TAssignedSet& inAssigned)
{
    auto symbolId = GetSymbolId(ident->Name, scopeId, ident);
    if (!symbolId) {
        return std::unexpected(symbolId.error());
    }

    int id = *symbolId;

    auto symNode = Context.GetSymbolNode(TSymbolId{id});

    if (TMaybeNode<TVarStmt>(symNode)) {
        if (!inAssigned.contains(id)) {
            return std::unexpected(TError(
                ident->Location,
                "Переменная '" + ident->Name + "' используется до первого присваивания значения. "
                "Необходимо присвоить значение перед использованием."));
        }
    }

    return inAssigned;
}

std::expected<TDefiniteAssignmentChecker::TAssignedSet, TError>
TDefiniteAssignmentChecker::CheckFunDecl(
    const std::shared_ptr<TFunDecl>& funDecl,
    TScopeId /*scopeId*/,
    const TAssignedSet& inAssigned)
{
    TAssignedSet initialAssigned = GlobalAssigned;

    TScopeId bodyScope{funDecl->Body->Scope};

    for (auto& param : funDecl->Params) {
        auto symbolId = Context.Lookup(param->Name, bodyScope);
        if (!symbolId) {
            return std::unexpected(TError(
                param->Location,
                "Параметр '" + param->Name + "' не определён."));
        }

        initialAssigned.insert(symbolId->Id);
    }

    auto bodyRes = CheckExpr(funDecl->Body, bodyScope, initialAssigned);
    if (!bodyRes) {
        return std::unexpected(bodyRes.error());
    }

    return inAssigned;
}

} // namespace NSemantics
} // namespace NQumir
