#include "type_annotation.h"

#include <qumir/optional.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/parser/lexer.h>

#include <iostream>
#include <sstream>

namespace NQumir {
namespace NTypeAnnotation {

using namespace NAst;

namespace {

using TTask = TExpectedTask<TExprPtr, TError, TLocation>;

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId);

bool WideningIntOK(TTypePtr typeSrc, TTypePtr typeDst) {
    auto src = TMaybeType<TIntegerType>(typeSrc).Cast();
    auto dst = TMaybeType<TIntegerType>(typeDst).Cast();
    if (!src || !dst) {
        return false;
    }

    // For now we have only one integer type, so this is trivial
    int wSrc = 64;
    int wDst = 64;
    bool signedSrc = true; // we have only signed int for now
    bool signedDst = true; // we have only signed int for now

    if (signedSrc == signedDst) {
        return wDst >= wSrc;
    }

    if (!signedSrc && signedDst) {
        // unsigned -> signed
        return wDst > wSrc; // u8->i16, u16->i32
    }

    if (signedSrc && !signedDst) {
        // signed -> unsigned: forbidden
        return false;
    }

    return false;
}

bool EqualTypes(TTypePtr a, TTypePtr b) {
    if (a->TypeName() != b->TypeName()) {
        return false;
    }

    auto maybeAInt = TMaybeType<TIntegerType>(a);
    auto maybeBInt = TMaybeType<TIntegerType>(b);
    if (maybeAInt && maybeBInt) {
        // we have 1 int type for now
        return true;
    }

    auto maybeAFloat = TMaybeType<TFloatType>(a);
    auto maybeBFloat = TMaybeType<TFloatType>(b);
    if (maybeAFloat && maybeBFloat) {
        // we hame 1 float type for now
        return true;
    }

    return true;
}

bool CanImplicit(TTypePtr S, TTypePtr D) {
    if (EqualTypes(S, D)) {
        return true;
    }

    if (TMaybeType<TIntegerType>(S) && TMaybeType<TIntegerType>(D)) {
        return WideningIntOK(S, D);
    }
    if (TMaybeType<TIntegerType>(S) && TMaybeType<TFloatType>(D)) {
        return true;
    }
    if (TMaybeType<TFloatType>(S) && TMaybeType<TIntegerType>(D)) {
        return false;
    }
    if ((TMaybeType<TFloatType>(S) || TMaybeType<TIntegerType>(S)) && TMaybeType<TBoolType>(D)) {
        return true;
    }

    auto maybePointerS = TMaybeType<TPointerType>(S);
    auto maybePointerD = TMaybeType<TPointerType>(D);

    if (maybePointerS && maybePointerD) {
        if (TMaybeType<TVoidType>(maybePointerD.Cast()->PointeeType)) {
            return true; // T* -> void*
        }
        if (TMaybeType<TVoidType>(maybePointerS.Cast()->PointeeType)) {
            return false; // void* -> T* (requires cast)
        }
        return EqualTypes(
            maybePointerD.Cast()->PointeeType,
            maybePointerS.Cast()->PointeeType);
    }

    return false;
}

TExprPtr AnnotateNumber(std::shared_ptr<TNumberExpr> num) {
    if (num->IsFloat) {
        num->Type = std::make_shared<NAst::TFloatType>();
    } else {
        num->Type = std::make_shared<NAst::TIntegerType>();
    }
    return num;
}

TTask AnnotateUnary(std::shared_ptr<TUnaryExpr> unary, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    unary->Operand = co_await DoAnnotate(unary->Operand, context, scopeId);
    unary->Type = unary->Operand->Type;
    if (unary->Operator == '-') {
        auto maybeInt = TMaybeType<TIntegerType>(unary->Type);
        if (maybeInt) {
            auto intType = maybeInt.Cast();
            if (false /*!intType->IsSigned()*/) {
                co_return TError(unary->Location, "cannot negate unsigned integer type");
            }
            co_return unary;
        }
        auto maybeFloat = TMaybeType<TFloatType>(unary->Type);
        if (maybeFloat) {
            co_return unary;
        }
        co_return TError(unary->Location, "cannot negate non-numeric type");
    }
    co_return unary;
}

TTask AnnotateFunDecl(std::shared_ptr<TFunDecl> funDecl, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    std::vector<TTypePtr> params;
    for (auto& p : funDecl->Params) {
        co_await DoAnnotate(p, context, NSemantics::TScopeId{funDecl->Body->Scope});
        if (!p->Type) {
            co_return TError(p->Location, "untyped function parameter: " + p->Name);
        }
        params.push_back(p->Type);
    }
    if (funDecl->RetType) {
        // early type check for recursive functions
        auto type = std::make_shared<TFunctionType>(std::move(params), funDecl->RetType);
        funDecl->Type = type;
    }

    co_await DoAnnotate(funDecl->Body, context, scopeId);
    if (!funDecl->Body->Type) {
        co_return TError(funDecl->Location, "function body type unknown");
    }
    if (funDecl->RetType && !CanImplicit(funDecl->Body->Type, funDecl->RetType)) {
        co_return TError(funDecl->Location, "function body type incompatible with declared return type");
    }

    if (!funDecl->Type) {
        funDecl->Type = std::make_shared<TFunctionType>(std::move(params), funDecl->Body->Type);
        funDecl->RetType = funDecl->Body->Type;
    }

    co_return funDecl;
}

TTask AnnotateBinary(std::shared_ptr<TBinaryExpr> binary, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    binary->Left = co_await DoAnnotate(binary->Left, context, scopeId);
    binary->Right = co_await DoAnnotate(binary->Right, context, scopeId);
    if (!binary->Left->Type || !binary->Right->Type) {
        co_return TError(binary->Location, "cannot type binary expression with untyped operand");
    }
    auto left = binary->Left->Type;
    auto right = binary->Right->Type;

    switch (binary->Operator) {
        case TOperator("+"):
        case TOperator("-"):
        case TOperator("*"):
        case TOperator("/"):
            if (TMaybeType<TFloatType>(left) && TMaybeType<TFloatType>(right)) {
                binary->Type = std::make_shared<TFloatType>();
            } else if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                binary->Type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both numeric types");
            }
            break;
        case TOperator("%"):
            // integer remainder
            if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                binary->Type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both integer types");
            }
            break;
        case TOperator("**"):
            // power: left^right
            if (TMaybeType<TFloatType>(left) && TMaybeType<TIntegerType>(right)) {
                binary->Type = std::make_shared<TFloatType>();
            } else if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                binary->Type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both numeric types (float^int or int^int)");
            }
            break;

        case TOperator("<"):
        case TOperator("<="):
        case TOperator(">"):
        case TOperator(">="):
        case TOperator("=="):
        case TOperator("!="):
        case TOperator("&&"):
        case TOperator("||"):
            binary->Type = std::make_shared<TBoolType>();
            break;
        default:
            co_return TError(binary->Location, "unknown binary operator: " + binary->Operator.ToString());
            break;
    }

    co_return binary;
}

TTask AnnotateBlock(std::shared_ptr<TBlockExpr> block, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    TTypePtr lastType = nullptr;
    for (auto& s : block->Stmts) {
        s = co_await DoAnnotate(s, context, NSemantics::TScopeId{block->Scope});
        if (!s->Type) {
            co_return TError(s->Location, "statement in block has no type");
        }
        lastType = s->Type;
    }
    if (!lastType) {
        lastType = std::make_shared<TVoidType>();
    }
    block->Type = lastType;
    co_return block;
}

TTask AnnotateAssign(std::shared_ptr<TAssignExpr> assign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    assign->Value = co_await DoAnnotate(assign->Value, context, scopeId);
    if (!assign->Value->Type) {
        co_return TError(assign->Location, "cannot assign untyped value to: " + assign->Name);
    }
    assign->Type = std::make_shared<NAst::TVoidType>();
    co_return assign;
}

TTask AnnotateVar(std::shared_ptr<TVarStmt> var) {
    if (!var->Type) {
        co_return TError(var->Location, "untyped var declaration: " + var->Name);
    }
    co_return var;
}

TTask AnnotateIdent(std::shared_ptr<TIdentExpr> ident, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    auto symbolId = context.Lookup(ident->Name, scopeId);
    if (!symbolId) {
        co_return TError(ident->Location, "undefined identifier: " + ident->Name);
    }
    auto sym = context.GetSymbolNode(*symbolId);
    if (!sym) {
        co_return TError(ident->Location, "invalid identifier symbol: " + ident->Name);
    }
    ident->Type = sym->Type;
    if (!ident->Type) {
        co_return TError(ident->Location, "untyped identifier: " + ident->Name);
    }
    co_return ident;
}

TTask AnnotateCall(std::shared_ptr<TCallExpr> call, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    call->Callee = co_await DoAnnotate(call->Callee, context, scopeId);
    if (!call->Callee->Type) {
        co_return TError(call->Location, "cannot call untyped expression");
    }
    for (auto& arg : call->Args) {
        arg = co_await DoAnnotate(arg, context, scopeId);
        if (!arg->Type) {
            co_return TError(arg->Location, "cannot pass untyped argument in function call");
        }
    }
    auto maybeFunType = TMaybeType<TFunctionType>(call->Callee->Type);
    if (maybeFunType) {
        call->Type = maybeFunType.Cast()->ReturnType;
    } else {
        call->Type = call->Callee->Type;
    }
    co_return call;
}

TTask AnnotateIf(std::shared_ptr<TIfExpr> ifExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    ifExpr->Cond = co_await DoAnnotate(ifExpr->Cond, context, scopeId);
    if (!ifExpr->Cond->Type) {
        co_return TError(ifExpr->Cond->Location, "untyped condition in if");
    }
    ifExpr->Then = co_await DoAnnotate(ifExpr->Then, context, scopeId);
    if (!ifExpr->Then->Type) {
        co_return TError(ifExpr->Then->Location, "untyped then branch in if");
    }
    ifExpr->Else = co_await DoAnnotate(ifExpr->Else, context, scopeId);
    if (!ifExpr->Else->Type) {
        co_return TError(ifExpr->Else->Location, "untyped else branch in if");
    }
    if (CanImplicit(ifExpr->Then->Type, ifExpr->Else->Type)) {
        ifExpr->Type = ifExpr->Else->Type;
    } else if (CanImplicit(ifExpr->Else->Type, ifExpr->Then->Type)) {
        ifExpr->Type = ifExpr->Then->Type;
    } else if (EqualTypes(ifExpr->Then->Type, ifExpr->Else->Type)) {
        ifExpr->Type = ifExpr->Then->Type;
    } else {
        co_return TError(ifExpr->Location, "incompatible types in then and else branches of if");
    }
    co_return ifExpr;
}

TTask AnnotateLoop(std::shared_ptr<TLoopStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    loop->Type = std::make_shared<TVoidType>();

    for (auto& child : loop->Children()) {
        if (child && !child->Type) {
            child = co_await DoAnnotate(child, context, scopeId);
        }
    }

    co_return loop;
}

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    if (expr->Type) {
        for (auto& child : expr->Children()) {
            if (child && !child->Type) {
                child = co_await DoAnnotate(child, context, scopeId);
            }
        }
        co_return expr;
    }

    if (auto maybeNum = TMaybeNode<TNumberExpr>(expr)) {
        co_return AnnotateNumber(maybeNum.Cast());
    } else if (auto maybeUnary = TMaybeNode<TUnaryExpr>(expr)) {
        co_return co_await AnnotateUnary(maybeUnary.Cast(), context, scopeId);
    } else if (auto maybeBinary = TMaybeNode<TBinaryExpr>(expr)) {
        co_return co_await AnnotateBinary(maybeBinary.Cast(), context, scopeId);
    } else if (auto maybeBlock = TMaybeNode<TBlockExpr>(expr)) {
        co_return co_await AnnotateBlock(maybeBlock.Cast(), context, scopeId);
    } else if (auto maybeIdent = TMaybeNode<TIdentExpr>(expr)) {
        co_return co_await AnnotateIdent(maybeIdent.Cast(), context, scopeId);
    } else if (auto maybeAssign = TMaybeNode<TAssignExpr>(expr)) {
        co_return co_await AnnotateAssign(maybeAssign.Cast(), context, scopeId);
    } else if (auto maybeVar = TMaybeNode<TVarStmt>(expr)) {
        co_return co_await AnnotateVar(maybeVar.Cast());
    } else if (auto maybeFunDecl = TMaybeNode<TFunDecl>(expr)) {
        co_return co_await AnnotateFunDecl(maybeFunDecl.Cast(), context, scopeId);
    } else if (auto maybeCall = TMaybeNode<TCallExpr>(expr)) {
        co_return co_await AnnotateCall(maybeCall.Cast(), context, scopeId);
    } else if (auto maybeIf = TMaybeNode<TIfExpr>(expr)) {
        co_return co_await AnnotateIf(maybeIf.Cast(), context, scopeId);
    } else if (TMaybeNode<TBreakStmt>(expr)) {
        expr->Type = std::make_shared<TVoidType>();
        co_return expr;
    } else if (TMaybeNode<TContinueStmt>(expr)) {
        expr->Type = std::make_shared<TVoidType>();
        co_return expr;
    } else if (auto maybeLoop = TMaybeNode<TLoopStmtExpr>(expr)) {
        auto loop = maybeLoop.Cast();
        co_return co_await AnnotateLoop(loop, context, scopeId);
    } else {
        co_return TError(expr->Location,
            std::string("unknown expression type for type annotation: ") + std::string(expr->NodeName()));
    }
}

} // namespace

TTypeAnnotator::TTypeAnnotator(NSemantics::TNameResolver& context)
    : Context(context)
{}

std::expected<TExprPtr, TError> TTypeAnnotator::Annotate(TExprPtr expr)
{
    return DoAnnotate(expr, Context, NSemantics::TScopeId{0}).result();
}

} // namespace NTypeAnnotation
} // namespace NQumir
