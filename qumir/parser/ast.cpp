#include "ast.h"

#include <stdexcept>

namespace NQumir {
namespace NAst {

TExprPtr ShallowCloneNode(const TExprPtr& node) {
    if (auto n = TMaybeNode<TIdentExpr>(node)) {
        return std::make_shared<TIdentExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TAssignExpr>(node)) {
        return std::make_shared<TAssignExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TArrayAssignExpr>(node)) {
        return std::make_shared<TArrayAssignExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TNumberExpr>(node)) {
        return std::make_shared<TNumberExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TStringLiteralExpr>(node)) {
        return std::make_shared<TStringLiteralExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TUnaryExpr>(node)) {
        return std::make_shared<TUnaryExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TBinaryExpr>(node)) {
        return std::make_shared<TBinaryExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TBlockExpr>(node)) {
        return std::make_shared<TBlockExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TIfExpr>(node)) {
        return std::make_shared<TIfExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TWhileStmtExpr>(node)) {
        return std::make_shared<TWhileStmtExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TRepeatStmtExpr>(node)) {
        return std::make_shared<TRepeatStmtExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TForStmtExpr>(node)) {
        return std::make_shared<TForStmtExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TTimesStmtExpr>(node)) {
        return std::make_shared<TTimesStmtExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TBreakStmt>(node)) {
        return std::make_shared<TBreakStmt>(*n.Cast());
    }
    if (auto n = TMaybeNode<TContinueStmt>(node)) {
        return std::make_shared<TContinueStmt>(*n.Cast());
    }
    if (auto n = TMaybeNode<TReturnExpr>(node)) {
        return std::make_shared<TReturnExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TVarStmt>(node)) {
        return std::make_shared<TVarStmt>(*n.Cast());
    }
    if (auto n = TMaybeNode<TVarsBlockExpr>(node)) {
        return std::make_shared<TVarsBlockExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TFunDecl>(node)) {
        return std::make_shared<TFunDecl>(*n.Cast());
    }
    if (auto n = TMaybeNode<TCallExpr>(node)) {
        return std::make_shared<TCallExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TAwaitExpr>(node)) {
        return std::make_shared<TAwaitExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TInputExpr>(node)) {
        return std::make_shared<TInputExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TOutputExpr>(node)) {
        return std::make_shared<TOutputExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TCastExpr>(node)) {
        return std::make_shared<TCastExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TBitcastExpr>(node)) {
        return std::make_shared<TBitcastExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TIndexExpr>(node)) {
        return std::make_shared<TIndexExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TMultiIndexExpr>(node)) {
        return std::make_shared<TMultiIndexExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TSliceExpr>(node)) {
        return std::make_shared<TSliceExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TUseExpr>(node)) {
        return std::make_shared<TUseExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TAssertStmt>(node)) {
        return std::make_shared<TAssertStmt>(*n.Cast());
    }
    if (auto n = TMaybeNode<TTypeDeclStmt>(node)) {
        return std::make_shared<TTypeDeclStmt>(*n.Cast());
    }
    if (auto n = TMaybeNode<TFieldAccessExpr>(node)) {
        return std::make_shared<TFieldAccessExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TStructConstructExpr>(node)) {
        return std::make_shared<TStructConstructExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TFieldAssignExpr>(node)) {
        return std::make_shared<TFieldAssignExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TRetainExpr>(node)) {
        return std::make_shared<TRetainExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TOwnLiteralExpr>(node)) {
        return std::make_shared<TOwnLiteralExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TMoveExpr>(node)) {
        return std::make_shared<TMoveExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TBorrowExpr>(node)) {
        return std::make_shared<TBorrowExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TDestroyExpr>(node)) {
        return std::make_shared<TDestroyExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TReplaceExpr>(node)) {
        return std::make_shared<TReplaceExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TCleanupExitExpr>(node)) {
        return std::make_shared<TCleanupExitExpr>(*n.Cast());
    }
    if (auto n = TMaybeNode<TGlobalCleanupExpr>(node)) {
        return std::make_shared<TGlobalCleanupExpr>(*n.Cast());
    }
    return node;
}

TExprPtr DeepCloneExpr(const TExprPtr& node) {
    if (!node) {
        return nullptr;
    }
    auto clone = ShallowCloneNode(node);
    if (clone == node) {
        throw std::logic_error(
            "DeepCloneExpr does not support AST node "
            + std::string(node->NodeName()));
    }
    if (auto block = TMaybeNode<TBlockExpr>(clone)) {
        block.Cast()->Scope = -1;
    } else if (auto function = TMaybeNode<TFunDecl>(clone)) {
        function.Cast()->Scope = -1;
        for (auto& param : function.Cast()->Params) {
            param = std::static_pointer_cast<TVarStmt>(DeepCloneExpr(param));
        }
    } else if (auto vars = TMaybeNode<TVarsBlockExpr>(clone)) {
        for (auto& var : vars.Cast()->Vars) {
            var = DeepCloneExpr(var);
        }
    } else if (auto use = TMaybeNode<TUseExpr>(clone)) {
        use.Cast()->Resolved = false;
    }
    for (auto* child : clone->MutableChildren()) {
        *child = DeepCloneExpr(*child);
    }
    return clone;
}

void TIdentExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TArrayAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TNumberExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TStringLiteralExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TUnaryExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBinaryExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBlockExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TIfExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TWhileStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TRepeatStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TForStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TTimesStmtExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBreakStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TContinueStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TReturnExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TVarStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TVarsBlockExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFunDecl::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCallExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAwaitExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TInputExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TOutputExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCastExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBitcastExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TIndexExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TMultiIndexExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TSliceExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TUseExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TAssertStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TTypeDeclStmt::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFieldAccessExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TStructConstructExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TFieldAssignExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TRetainExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TOwnLiteralExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TMoveExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TBorrowExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TDestroyExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TReplaceExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TCleanupExitExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }
void TGlobalCleanupExpr::Accept(IVisitor& visitor) { visitor.Visit(*this); }

} // namespace NAst
} // namespace NQumir
