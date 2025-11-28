#pragma once

#include <qumir/parser/ast.h>
#include <qumir/error.h>
#include <qumir/optional.h>

#include "qumir/semantics/name_resolution/name_resolver.h"

namespace NQumir {
namespace NSemantics {

class TDefiniteAssignmentChecker {
public:
    explicit TDefiniteAssignmentChecker(TNameResolver& context);

    std::expected<void, TError> Check(NAst::TExprPtr root);

private:
    using TAssignedSet = std::unordered_set<int>; // assigned symbol ids

    std::expected<TAssignedSet, TError> CheckExpr(
        const NAst::TExprPtr& expr,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckBlock(
        const std::shared_ptr<NAst::TBlockExpr>& block,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckIf(
        const std::shared_ptr<NAst::TIfExpr>& ifExpr,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckLoop(
        const std::shared_ptr<NAst::TLoopStmtExpr>& loop,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckAssign(
        const std::shared_ptr<NAst::TAssignExpr>& assign,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckVar(
        const std::shared_ptr<NAst::TVarStmt>& var,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckIdent(
        const std::shared_ptr<NAst::TIdentExpr>& ident,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckFunDecl(
        const std::shared_ptr<NAst::TFunDecl>& funDecl,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    std::expected<TAssignedSet, TError> CheckCall(
        const std::shared_ptr<NAst::TCallExpr>& call,
        TScopeId scopeId,
        const TAssignedSet& inAssigned);

    static TAssignedSet Intersect(const TAssignedSet& a, const TAssignedSet& b);
    static TAssignedSet Union(const TAssignedSet& a, const TAssignedSet& b);

    std::expected<int, TError> GetSymbolId(
        const std::string& name,
        TScopeId scopeId,
        const NAst::TExprPtr& where);
    std::expected<NAst::TExprPtr, TError> GetSymbolNode(
        const std::string& name,
        TScopeId scopeId,
        const NAst::TExprPtr& where);

    TNameResolver& Context;
    TAssignedSet GlobalAssigned;
};

} // namespace NSemantics
} // namespace NQumir