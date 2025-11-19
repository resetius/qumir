#include "type_annotation.h"

#include <qumir/optional.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/parser/lexer.h>

#include <iostream>
#include <sstream>
#include <cassert>

namespace NQumir {
namespace NTypeAnnotation {

using namespace NAst;

namespace {

using TTask = TExpectedTask<TExprPtr, TError, TLocation>;

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId);

TTypePtr UnwrapReferenceType(TTypePtr type) {
    if (auto maybeRef = TMaybeType<TReferenceType>(type)) {
        return maybeRef.Cast()->ReferencedType;
    }
    return type;
}

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
        return true; // float to int conversion allowed
    }
    if ((TMaybeType<TFloatType>(S) || TMaybeType<TIntegerType>(S)) && TMaybeType<TBoolType>(D)) {
        return true;
    }
    if (TMaybeType<TSymbolType>(S) && TMaybeType<TStringType>(D)) {
        return true; // symbol to string conversion allowed
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

TExprPtr InsertImplicitCastIfNeeded(TExprPtr expr, TTypePtr toType) {
    if (!expr->Type || !toType) {
        return expr;
    }
    if (EqualTypes(expr->Type, toType)) {
        return expr;
    }
    if (!CanImplicit(expr->Type, toType)) {
        return expr;
    }

    if (auto maybeNumber = TMaybeNode<TNumberExpr>(expr)) {
        auto num = maybeNumber.Cast();
        if (TMaybeType<TIntegerType>(toType)) {
            if (num->IsFloat) {
                // float to int
                int64_t intVal = static_cast<int64_t>(num->FloatValue);
                auto newNum = std::make_shared<TNumberExpr>(num->Location, intVal);
                newNum->Type = toType;
                return newNum;
            } else {
                // int to int (widening)
                num->Type = toType;
                return num;
            }
        }
        if (TMaybeType<TFloatType>(toType)) {
            if (num->IsFloat) {
                // float to float (widening)
                num->Type = toType;
                return num;
            } else {
                // int to float
                double floatVal = static_cast<double>(num->IntValue);
                auto newNum = std::make_shared<TNumberExpr>(num->Location, floatVal);
                newNum->Type = toType;
                return newNum;
            }
        }
    }

    return MakeCast(std::move(expr), std::move(toType));
}

TTypePtr CommonNumericType(TTypePtr a, TTypePtr b) {
    if (TMaybeType<TFloatType>(a) && TMaybeType<TFloatType>(b)) {
        return a;
    }
    if (TMaybeType<TFloatType>(a) && TMaybeType<TIntegerType>(b)) {
        return a;
    }
    if (TMaybeType<TIntegerType>(a) && TMaybeType<TFloatType>(b)) {
        return b;
    }
    if (TMaybeType<TIntegerType>(a) && TMaybeType<TIntegerType>(b)) {
        return a;
    }
    return {};
}

TExprPtr AnnotateNumber(std::shared_ptr<TNumberExpr> num) {
    if (num->IsFloat) {
        num->Type = std::make_shared<TFloatType>();
    } else {
        num->Type = std::make_shared<TIntegerType>();
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
    if (unary->Operator == TOperator("!")) {
        auto maybeBool = TMaybeType<TBoolType>(unary->Type);
        if (maybeBool) {
            unary->Type = std::make_shared<TBoolType>();
            co_return unary;
        }
        auto maybeInt = TMaybeType<TIntegerType>(unary->Type);
        if (maybeInt) {
            unary->Type = std::make_shared<TBoolType>();
            co_return unary;
        }
        auto maybeFloat = TMaybeType<TFloatType>(unary->Type);
        if (maybeFloat) {
            unary->Type = std::make_shared<TBoolType>();
            co_return unary;
        }
        co_return TError(unary->Location, "cannot apply '!' to non-boolean type");
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
    if (!funDecl->RetType) {
        co_return TError(funDecl->Location, "function return type unknown");
    }

    funDecl->Type = std::make_shared<TFunctionType>(std::move(params), funDecl->RetType);

    co_await DoAnnotate(funDecl->Body, context, scopeId);
    if (!funDecl->Body->Type) {
        // function body type always set to void
        co_return TError(funDecl->Location, "function body type unknown");
    }

    co_return funDecl;
}

TTask AnnotateBinary(std::shared_ptr<TBinaryExpr> binary, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    binary->Left = co_await DoAnnotate(binary->Left, context, scopeId);
    binary->Right = co_await DoAnnotate(binary->Right, context, scopeId);
    if (!binary->Left->Type || !binary->Right->Type) {
        co_return TError(binary->Location, "cannot type binary expression with untyped operand");
    }
    auto left = UnwrapReferenceType(binary->Left->Type);
    auto right = UnwrapReferenceType(binary->Right->Type);

    switch (binary->Operator) {
        case TOperator("+"):
        case TOperator("-"):
        case TOperator("*"):
        case TOperator("/"): {
            // string and symbol concatenation cases
            if (binary->Operator == TOperator("+")) {
                auto maybeStrLeft = TMaybeType<TStringType>(left);
                auto maybeStrRight = TMaybeType<TStringType>(right);
                auto maybeSymLeft = TMaybeType<TSymbolType>(left);
                auto maybeSymRight = TMaybeType<TSymbolType>(right);

                // string + string
                if (maybeStrLeft && maybeStrRight) {
                    binary->Type = left;
                    break;
                }
                // string + symbol
                if (maybeStrLeft && maybeSymRight) {
                    binary->Right = InsertImplicitCastIfNeeded(binary->Right, maybeStrLeft.Cast());
                    binary->Type = maybeStrLeft.Cast();
                    break;
                }
                // symbol + string
                if (maybeSymLeft && maybeStrRight) {
                    binary->Left = InsertImplicitCastIfNeeded(binary->Left, maybeStrRight.Cast());
                    binary->Type = maybeStrRight.Cast();
                    break;
                }
                // symbol + symbol => string
                if (maybeSymLeft && maybeSymRight) {
                    auto strT = std::make_shared<TStringType>();
                    binary->Left = InsertImplicitCastIfNeeded(binary->Left, strT);
                    binary->Right = InsertImplicitCastIfNeeded(binary->Right, strT);
                    binary->Type = strT;
                    break;
                }
            }

            auto common = CommonNumericType(left, right);
            if (!common) {
                co_return TError(binary->Location, "arithmetic operands must be numbers");
            }

            binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  common);
            binary->Right = InsertImplicitCastIfNeeded(binary->Right, common);
            binary->Type  = common;
            break;
        }
        case TOperator("%"):
            // integer remainder
            if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                binary->Type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both integer types");
            }
            break;
        case TOperator("^"):
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
            if ((TMaybeType<TFloatType>(left) || TMaybeType<TIntegerType>(left)) &&
                (TMaybeType<TFloatType>(right) || TMaybeType<TIntegerType>(right))) {
                auto common = CommonNumericType(left, right);
                if (!common) {
                    co_return TError(binary->Location, "comparison requires numeric operands");
                }
                binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  common);
                binary->Right = InsertImplicitCastIfNeeded(binary->Right, common);
            }
            binary->Type = std::make_shared<TBoolType>();
            break;
        case TOperator("&&"):
        case TOperator("||"):
            binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  std::make_shared<TBoolType>());
            binary->Right = InsertImplicitCastIfNeeded(binary->Right, std::make_shared<TBoolType>());
            binary->Type  = std::make_shared<TBoolType>();
            break;
        default:
            co_return TError(binary->Location, "unknown binary operator: " + binary->Operator.ToString());
            break;
    }

    co_return binary;
}

TTask AnnotateBlock(std::shared_ptr<TBlockExpr> block, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    TExprPtr lastExpr;
    for (auto& s : block->Stmts) {
        s = co_await DoAnnotate(s, context, NSemantics::TScopeId{block->Scope});
        if (!s->Type) {
            co_return TError(s->Location, "statement in block has no type");
        }
        lastExpr = s;
    }
    if (lastExpr) {
        block->Type = lastExpr->Type;
    } else {
        block->Type = std::make_shared<TVoidType>();
    }
    co_return block;
}

TTask AnnotateAssign(std::shared_ptr<TAssignExpr> assign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    assign->Value = co_await DoAnnotate(assign->Value, context, scopeId);
    if (!assign->Value->Type) {
        co_return TError(assign->Location, "cannot assign untyped value to: " + assign->Name);
    }
    auto symbolId = context.Lookup(assign->Name, scopeId);
    if (!symbolId) {
        co_return TError(assign->Location, "undefined identifier in assignment: " + assign->Name);
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym || !sym->Type) {
        co_return TError(assign->Location, "untyped identifier in assignment: " + assign->Name);
    }
    auto symbolType = UnwrapReferenceType(sym->Type);

    if (!symbolType->Mutable) {
        co_return TError(assign->Location, "cannot assign to immutable variable: " + assign->Name);
    }

    auto valueType = UnwrapReferenceType(assign->Value->Type);
    if (!EqualTypes(valueType, symbolType)) {
        if (!CanImplicit(valueType, symbolType)) {
            co_return TError(assign->Location, std::string("cannot implicitly convert '") +
                std::string(valueType->TypeName()) + "' to '" + std::string(symbolType->TypeName()) + "' in assignment");
        }
        assign->Value = InsertImplicitCastIfNeeded(assign->Value, sym->Type);
    }

    assign->Type = std::make_shared<NAst::TVoidType>();
    co_return assign;
}

TTask AnnotateArrayAssign(std::shared_ptr<TArrayAssignExpr> arrayAssign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    arrayAssign->Value = co_await DoAnnotate(arrayAssign->Value, context, scopeId);
    if (!arrayAssign->Value->Type) {
        co_return TError(arrayAssign->Location, "cannot assign untyped value to array: " + arrayAssign->Name);
    }
    auto symbolId = context.Lookup(arrayAssign->Name, scopeId);
    if (!symbolId) {
        co_return TError(arrayAssign->Location, "undefined identifier in array assignment: " + arrayAssign->Name);
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym || !sym->Type) {
        co_return TError(arrayAssign->Location, "untyped identifier in array assignment: " + arrayAssign->Name);
    }
    auto maybeArrayType = TMaybeType<TArrayType>(sym->Type);
    if (!maybeArrayType) {
        co_return TError(arrayAssign->Location, "identifier is not an array in array assignment: " + arrayAssign->Name);
    }
    auto arrayType = maybeArrayType.Cast();

    auto valueType = UnwrapReferenceType(arrayAssign->Value->Type);
    if (!EqualTypes(valueType, arrayType->ElementType)) {
        if (!CanImplicit(valueType, arrayType->ElementType)) {
            co_return TError(arrayAssign->Location, std::string("cannot implicitly convert '") +
                std::string(valueType->TypeName()) + "' to '" + std::string(arrayType->ElementType->TypeName()) + "' in array assignment");
        }
        arrayAssign->Value = InsertImplicitCastIfNeeded(arrayAssign->Value, arrayType->ElementType);
    }
    for (auto& indexExpr : arrayAssign->Indices) {
        indexExpr = co_await DoAnnotate(indexExpr, context, scopeId);
        if (!indexExpr->Type) {
            co_return TError(arrayAssign->Location, "untyped index expression in array assignment: " + arrayAssign->Name);
        }
        auto maybeIntType = TMaybeType<TIntegerType>(indexExpr->Type);
        if (!maybeIntType) {
            co_return TError(arrayAssign->Location, "non-integer index expression in array assignment: " + arrayAssign->Name);
        }
    }
    // check arity
    if (arrayAssign->Indices.size() != arrayType->Arity) {
        co_return TError(arrayAssign->Location, "invalid number of indices in array assignment: " + arrayAssign->Name);
    }

    arrayAssign->Type = std::make_shared<NAst::TVoidType>();
    co_return arrayAssign;
}

TTask AnnotateVar(std::shared_ptr<TVarStmt> var) {
    if (!var->Type) {
        co_return TError(var->Location, "untyped var declaration: " + var->Name);
    }
    co_return var;
}

TTask AnnotateIdent(std::shared_ptr<TIdentExpr> ident, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId, bool pathThrough = false) {
    auto symbolId = context.Lookup(ident->Name, scopeId);
    if (!symbolId) {
        co_return TError(ident->Location, "undefined identifier: " + ident->Name);
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym) {
        co_return TError(ident->Location, "invalid identifier symbol: " + ident->Name);
    }
    ident->Type = sym->Type;
    if (!ident->Type) {
        co_return TError(ident->Location, "untyped identifier: " + ident->Name);
    }
    if (pathThrough) {
        co_return ident;
    }
    auto unwrappedType = UnwrapReferenceType(ident->Type);
    if (!unwrappedType->Readable) {
        co_return TError(ident->Location, "cannot read from write-only identifier: " + ident->Name);
    }
    if (auto maybeArray = TMaybeType<TArrayType>(unwrappedType)) {
        auto arrayType = maybeArray.Cast();
        if (!arrayType->ElementType->Readable) {
            co_return TError(ident->Location, "cannot read from write-only array elements: " + ident->Name);
        }
    }

    co_return ident;
}

TTask AnnotateCall(std::shared_ptr<TCallExpr> call, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    call->Callee = co_await DoAnnotate(call->Callee, context, scopeId);
    if (!call->Callee->Type) {
        co_return TError(call->Location, "cannot call untyped expression");
    }
    auto maybeFunType = TMaybeType<TFunctionType>(call->Callee->Type);
    if (maybeFunType) {
        auto funT = maybeFunType.Cast();
        if (funT->ParamTypes.size() != call->Args.size()) {
            co_return TError(call->Location, "invalid number of arguments");
        }
        for (size_t i = 0; i < call->Args.size(); ++i) {
            auto& paramT = funT->ParamTypes[i];
            auto maybeRef = TMaybeType<TReferenceType>(paramT);

            auto& arg = call->Args[i];
            auto maybeIdent = TMaybeNode<TIdentExpr>(arg);
            // TODO: support array cells as ref arguments?
            if (maybeIdent) {
                arg = co_await AnnotateIdent(maybeIdent.Cast(), context, scopeId, /* path-through = */ true);
            } else {
                arg = co_await DoAnnotate(arg, context, scopeId);
            }
            if (!arg->Type) {
                co_return TError(arg->Location, "cannot pass untyped argument in function call");
            }

            // if ParamT is reference type, arg must be of the same referenced type or ident of that underlying type
            if (maybeRef) {
                auto paramRefT = maybeRef.Cast();
                if (maybeIdent) {
                    auto identType = UnwrapReferenceType(maybeIdent.Cast()->Type);
                    // ident must be writable
                    if (maybeIdent && !identType->Mutable) {
                        co_return TError(arg->Location, "cannot pass read-only identifier as reference argument");
                    }
                }
                auto argTypeUnwrapped = UnwrapReferenceType(arg->Type);
                if (!EqualTypes(argTypeUnwrapped, paramRefT->ReferencedType)) {
                    co_return TError(arg->Location, "cannot pass argument " + std::to_string(i+1) +
                        " of type '" + std::string(arg->Type->TypeName()) + "' to reference parameter of type '" +
                        std::string(paramT->TypeName()) + "'");
                }
                // no implicit cast for reference parameters
                continue;
            }
            if (!EqualTypes(arg->Type, paramT)) {
                if (!CanImplicit(UnwrapReferenceType(arg->Type), paramT)) {
                    co_return TError(arg->Location, "cannot implicitly convert argument " + std::to_string(i+1) +
                        " from '" + std::string(arg->Type->TypeName()) + "' to '" + std::string(paramT->TypeName()) + "'");
                }
                arg = InsertImplicitCastIfNeeded(arg, paramT);
            }
        }
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
    {
        auto boolT = std::make_shared<TBoolType>();
        auto condType = UnwrapReferenceType(ifExpr->Cond->Type);
        if (!EqualTypes(condType, boolT) && CanImplicit(condType, boolT)) {
            ifExpr->Cond = InsertImplicitCastIfNeeded(ifExpr->Cond, boolT);
        }
    }
    ifExpr->Then = co_await DoAnnotate(ifExpr->Then, context, scopeId);
    if (!ifExpr->Then->Type) {
        co_return TError(ifExpr->Then->Location, "untyped then branch in if");
    }
    if (ifExpr->Else) {
        ifExpr->Else = co_await DoAnnotate(ifExpr->Else, context, scopeId);
        if (!ifExpr->Else->Type) {
            co_return TError(ifExpr->Else->Location, "untyped else branch in if");
        }
    }
    // If is not expression, its type is always void
    ifExpr->Type = std::make_shared<TVoidType>();

    co_return ifExpr;
}

TTask AnnotateLoop(std::shared_ptr<TLoopStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    loop->Type = std::make_shared<TVoidType>();

    for (auto* child : loop->MutableChildren()) {
        if (*child) {
            *child = co_await DoAnnotate(*child, context, scopeId);
        }
    }

    co_return loop;
}

TTask AnnotateMultiIndex(std::shared_ptr<TMultiIndexExpr> indexExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    indexExpr->Collection = co_await DoAnnotate(indexExpr->Collection, context, scopeId);
    if (!indexExpr->Collection->Type) {
        co_return TError(indexExpr->Location, "untyped collection in multi-index expression");
    }
    auto maybeArrayType = TMaybeType<TArrayType>(indexExpr->Collection->Type);
    if (!maybeArrayType) {
        co_return TError(indexExpr->Location, "only array indexing is supported for now");
    }

    auto intType = std::make_shared<TIntegerType>();
    for (auto& index : indexExpr->Indices) {
        index = co_await DoAnnotate(index, context, scopeId);
        if (!index->Type) {
            co_return TError(index->Location, "untyped index in multi-index expression");
        }
        if (!EqualTypes(index->Type, intType)) {
            if (!CanImplicit(index->Type, intType)) {
                co_return TError(index->Location, "index expression requires integer index");
            }
            index = InsertImplicitCastIfNeeded(index, intType);
        }
    }
    auto arrayType = maybeArrayType.Cast();
    indexExpr->Type = arrayType->ElementType;

    co_return indexExpr;
}

TTask AnnotateIndex(std::shared_ptr<TIndexExpr> indexExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    indexExpr->Collection = co_await DoAnnotate(indexExpr->Collection, context, scopeId);
    if (!indexExpr->Collection->Type) {
        co_return TError(indexExpr->Location, "untyped collection in index expression");
    }
    indexExpr->Index = co_await DoAnnotate(indexExpr->Index, context, scopeId);
    if (!indexExpr->Index->Type) {
        co_return TError(indexExpr->Location, "untyped index in index expression");
    }
    auto intType = std::make_shared<TIntegerType>();
    if (!EqualTypes(indexExpr->Index->Type, intType)) {
        if (!CanImplicit(indexExpr->Index->Type, intType)) {
            co_return TError(indexExpr->Location, "index expression requires integer index");
        }
        indexExpr->Index = InsertImplicitCastIfNeeded(indexExpr->Index, intType);
    }
    if (TMaybeType<TStringType>(indexExpr->Collection->Type)) {
        indexExpr->Type = std::make_shared<TSymbolType>();
    } else if (auto maybeArrayType = TMaybeType<TArrayType>(indexExpr->Collection->Type)) {
        auto arrayType = maybeArrayType.Cast();
        indexExpr->Type = arrayType->ElementType;
    } else {
        co_return TError(indexExpr->Location, "unsupported collection type in index expression");
    }

    co_return indexExpr;
}

TTask AnnotateSlice(std::shared_ptr<TSliceExpr> sliceExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    sliceExpr->Collection = co_await DoAnnotate(sliceExpr->Collection, context, scopeId);
    if (!sliceExpr->Collection->Type) {
        co_return TError(sliceExpr->Location, "untyped collection in slice expression");
    }
    if (!TMaybeType<TStringType>(sliceExpr->Collection->Type)) {
        co_return TError(sliceExpr->Location, "only string slicing is supported for now");
    }
    sliceExpr->Start = co_await DoAnnotate(sliceExpr->Start, context, scopeId);
    if (!sliceExpr->Start->Type) {
        co_return TError(sliceExpr->Location, "untyped start index in slice expression");
    }
    sliceExpr->End = co_await DoAnnotate(sliceExpr->End, context, scopeId);
    if (!sliceExpr->End->Type) {
        co_return TError(sliceExpr->Location, "untyped end index in slice expression");
    }
    auto intType = std::make_shared<TIntegerType>();
    if (!EqualTypes(sliceExpr->Start->Type, intType)) {
        if (!CanImplicit(sliceExpr->Start->Type, intType)) {
            co_return TError(sliceExpr->Location, "slice expression requires integer start index");
        }
        sliceExpr->Start = InsertImplicitCastIfNeeded(sliceExpr->Start, intType);
    }
    // indexing a string yields a string (1-character substring)
    sliceExpr->Type = sliceExpr->Collection->Type;

    co_return sliceExpr;
}

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
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
    } else if (auto maybeArrayAssign = TMaybeNode<TArrayAssignExpr>(expr)) {
        co_return co_await AnnotateArrayAssign(maybeArrayAssign.Cast(), context, scopeId);
    } else if (auto maybeMultiIndex = TMaybeNode<TMultiIndexExpr>(expr)) {
        co_return co_await AnnotateMultiIndex(maybeMultiIndex.Cast(), context, scopeId);
    } else if (auto maybeVar = TMaybeNode<TVarStmt>(expr)) {
        co_return co_await AnnotateVar(maybeVar.Cast());
    } else if (auto maybeFunDecl = TMaybeNode<TFunDecl>(expr)) {
        co_return co_await AnnotateFunDecl(maybeFunDecl.Cast(), context, scopeId);
    } else if (auto maybeCall = TMaybeNode<TCallExpr>(expr)) {
        co_return co_await AnnotateCall(maybeCall.Cast(), context, scopeId);
    } else if (auto maybeIf = TMaybeNode<TIfExpr>(expr)) {
        co_return co_await AnnotateIf(maybeIf.Cast(), context, scopeId);
    } else if (auto maybeIndex = TMaybeNode<TIndexExpr>(expr)) {
        co_return co_await AnnotateIndex(maybeIndex.Cast(), context, scopeId);
    } else if (auto maybeSlice = TMaybeNode<TSliceExpr>(expr)) {
        co_return co_await AnnotateSlice(maybeSlice.Cast(), context, scopeId);
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
        if (!expr->Type) {
            // if expr->Type => node was annotated on construction
            co_return TError(expr->Location,
                std::string("unknown expression type for type annotation: ") + std::string(expr->NodeName()));
        }

        for (auto* child : expr->MutableChildren()) {
            if (*child) {
                *child = co_await DoAnnotate(*child, context, scopeId);
            }
        }
    }
    co_return expr;
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
