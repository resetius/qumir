#include "type_annotation.h"
#include "coercions.h"

#include <qumir/optional.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/type.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <iostream>
#include <sstream>
#include <cassert>

namespace NQumir {
namespace NTypeAnnotation {

using namespace NAst;

namespace {

using TTask = TExpectedTask<TExprPtr, TError, TLocation>;

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId);

// Defined further below, needed already in AnnotateFunDecl to skip
// body-annotation for template-parameterized declarations.
bool HasTemplateParams(const TFunDecl& decl);

bool WideningIntOK(TTypePtr typeSrc, TTypePtr typeDst) {
    auto src = TMaybeType<TIntegerType>(typeSrc).Cast();
    auto dst = TMaybeType<TIntegerType>(typeDst).Cast();
    if (!src || !dst) {
        return false;
    }

    int wSrc = src->BitWidth();
    int wDst = dst->BitWidth();
    bool signedSrc = src->IsSigned();
    bool signedDst = dst->IsSigned();

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
        return maybeAInt.Cast()->Kind == maybeBInt.Cast()->Kind;
    }

    auto maybeAFloat = TMaybeType<TFloatType>(a);
    auto maybeBFloat = TMaybeType<TFloatType>(b);
    if (maybeAFloat && maybeBFloat) {
        // we hame 1 float type for now
        return true;
    }

    auto maybeANamed = TMaybeType<TNamedType>(a);
    auto maybeBNamed = TMaybeType<TNamedType>(b);
    if (maybeANamed && maybeBNamed) {
        return maybeANamed.Cast()->Name == maybeBNamed.Cast()->Name;
    }

    auto maybeAFuture = TMaybeType<TFutureType>(a);
    auto maybeBFuture = TMaybeType<TFutureType>(b);
    if (maybeAFuture && maybeBFuture) {
        return EqualTypes(maybeAFuture.Cast()->ResultType, maybeBFuture.Cast()->ResultType);
    }

    return true;
}

bool CanImplicit(TTypePtr S, TTypePtr D, NSemantics::TNameResolver* ctx) {
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

    if (ctx->GetCast(S, D)) return true;
    return false;
}

// Returns the inline replacement if the function has an InlineFactory, nullptr otherwise.
// The replacement has no Type set — callers that are coroutines must DoAnnotate it.
TExprPtr TryApplyInlineFactory(const std::shared_ptr<TFunDecl>& funDecl, std::vector<TExprPtr> args) {
    if (funDecl->InlineFactory) {
        return (*funDecl->InlineFactory)(std::move(args));
    }
    return nullptr;
}

// Looks up a function by name in the given scope, then applies its InlineFactory if present.
TExprPtr TryInlineByName(const std::string& name, std::vector<TExprPtr> args,
                         NSemantics::TNameResolver& ctx, NSemantics::TScopeId scopeId) {
    auto symId = ctx.Lookup(name, scopeId);
    if (!symId) return nullptr;
    auto node = ctx.GetSymbolNode(NSemantics::TSymbolId{symId->Id});
    if (auto maybeFunDecl = TMaybeNode<TFunDecl>(node)) {
        return TryApplyInlineFactory(maybeFunDecl.Cast(), std::move(args));
    }
    return nullptr;
}

TExprPtr InsertImplicitCastIfNeeded(TExprPtr expr, TTypePtr toType, NSemantics::TNameResolver* ctx) {
    if (!expr->Type || !toType) {
        return expr;
    }
    if (EqualTypes(expr->Type, toType)) {
        return expr;
    }
    if (!CanImplicit(expr->Type, toType, ctx)) {
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

    if (auto synthName = ctx->GetCast(expr->Type, toType)) {
        auto callee = std::make_shared<TIdentExpr>(expr->Location, *synthName);
        callee->Type = std::make_shared<TFunctionType>(
            std::vector<TTypePtr>{expr->Type}, toType);
        auto call = std::make_shared<TCallExpr>(expr->Location, callee,
            std::vector<TExprPtr>{expr});
        call->Type = toType;
        return call;
    }

    return MakeCast(std::move(expr), std::move(toType));
}

// Annotates expr if its Type is not yet set (i.e. came from an inline factory replacement).
TTask AnnotateIfNeeded(TExprPtr expr, NSemantics::TNameResolver& ctx, NSemantics::TScopeId scopeId) {
    if (expr->Type) co_return expr;
    co_return co_await DoAnnotate(expr, ctx, scopeId);
}

TExprPtr MakeModuleOpCall(const std::string& synthName, std::vector<TExprPtr> args,
    TTypePtr returnType, TLocation loc, NSemantics::TNameResolver& ctx)
{
    auto callee = std::make_shared<TIdentExpr>(loc, synthName);
    std::vector<TTypePtr> argTypes;
    for (auto& a : args) argTypes.push_back(a->Type);
    callee->Type = std::make_shared<TFunctionType>(std::move(argTypes), returnType);
    auto call = std::make_shared<TCallExpr>(loc, callee, std::move(args));
    call->Type = returnType;
    return call;
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

TTypePtr CommonValueType(TTypePtr a, TTypePtr b, NSemantics::TNameResolver& ctx) {
    if (EqualTypes(a, b)) {
        return a;
    }

    if (auto numeric = CommonNumericType(a, b)) {
        return numeric;
    }

    if (CanImplicit(a, b, &ctx)) {
        return b;
    }

    if (CanImplicit(b, a, &ctx)) {
        return a;
    }

    return {};
}

TExprPtr AnnotateNumber(std::shared_ptr<TNumberExpr> num) {
    if (num->Type) {
        return num;
    }
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
                co_return TError(unary->Location, "Нельзя применять унарный минус к беззнаковому целому типу");
            }
            co_return unary;
        }
        auto maybeFloat = TMaybeType<TFloatType>(unary->Type);
        if (maybeFloat) {
            co_return unary;
        }
        if (auto m = context.GetUnaryOp("neg", UnwrapReferenceType(unary->Type))) {
            co_return co_await AnnotateIfNeeded(MakeModuleOpCall(m->SynthName, {unary->Operand}, m->ReturnType, unary->Location, context), context, scopeId);
        }
        co_return TError(unary->Location, "Нельзя применять унарный минус к нечисловому типу");
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
        co_return TError(unary->Location, "Оператор отрицания (не) применяется только к логическим выражениям");
    }
    if (unary->Operator == TOperator("~")) {
        if (TMaybeType<TIntegerType>(unary->Type)) {
            unary->Type = std::make_shared<TIntegerType>();
            co_return unary;
        }
        co_return TError(unary->Location, "Битовое отрицание применяется только к целым числам");
    }
    co_return unary;
}

TTask AnnotateFunDecl(std::shared_ptr<TFunDecl> funDecl, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    std::vector<TTypePtr> params;
    for (auto& p : funDecl->Params) {
        co_await DoAnnotate(p, context, NSemantics::TScopeId{funDecl->Body->Scope});
        if (!p->Type) {
            co_return TError(p->Location, "Не указан тип параметра функции: " + p->Name);
        }
        params.push_back(p->Type);
    }
    if (!funDecl->RetType) {
        co_return TError(funDecl->Location, "Не указан возвращаемый тип функции");
    }

    funDecl->Type = std::make_shared<TFunctionType>(std::move(params), funDecl->RetType);

    if (HasTemplateParams(*funDecl)) {
        // Generic (template-parameterized) function: its body refers to
        // placeholder types and cannot be type-checked on its own — only
        // monomorphized clones (created at call sites by
        // InstantiateGenericFunction) get annotated.
        co_return funDecl;
    }

    co_await DoAnnotate(funDecl->Body, context, scopeId);
    if (!funDecl->Body->Type) {
        co_return TError(funDecl->Location, "Не удалось определить тип тела функции");
    }

    co_return funDecl;
}

// Tries exact match, then cast-left, then cast-right. Returns TCallExpr or nullptr.
TExprPtr TryModuleBinaryOp(TExprPtr left, TExprPtr right,
    const TOperator& op, NSemantics::TNameResolver& ctx)
{
    const std::string opStr = op.ToString();
    auto lt = UnwrapReferenceType(left->Type);
    auto rt = UnwrapReferenceType(right->Type);

    if (auto m = ctx.GetBinaryOp(opStr, lt, rt)) {
        return MakeModuleOpCall(m->SynthName, {left, right}, m->ReturnType, left->Location, ctx);
    }

    auto castedLeft = InsertImplicitCastIfNeeded(left, rt, &ctx);
    if (castedLeft != left) {
        if (auto m = ctx.GetBinaryOp(opStr, rt, rt)) {
            return MakeModuleOpCall(m->SynthName, {castedLeft, right}, m->ReturnType, left->Location, ctx);
        }
    }

    auto castedRight = InsertImplicitCastIfNeeded(right, lt, &ctx);
    if (castedRight != right) {
        if (auto m = ctx.GetBinaryOp(opStr, lt, lt)) {
            return MakeModuleOpCall(m->SynthName, {left, castedRight}, m->ReturnType, left->Location, ctx);
        }
    }

    return nullptr;
}

TTask AnnotateBinary(std::shared_ptr<TBinaryExpr> binary, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    auto explicitType = binary->Type;
    binary->Left = co_await DoAnnotate(binary->Left, context, scopeId);
    binary->Right = co_await DoAnnotate(binary->Right, context, scopeId);
    if (!binary->Left->Type || !binary->Right->Type) {
        co_return TError(binary->Location, "Не удалось определить типы выражения для бинарной операции");
    }
    auto left = UnwrapReferenceType(binary->Left->Type);
    auto right = UnwrapReferenceType(binary->Right->Type);
    TTypePtr type;

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
                    type = left;
                    break;
                }
                // string + symbol
                if (maybeStrLeft && maybeSymRight) {
                    binary->Right = InsertImplicitCastIfNeeded(binary->Right, maybeStrLeft.Cast(), &context);
                    type = maybeStrLeft.Cast();
                    break;
                }
                // symbol + string
                if (maybeSymLeft && maybeStrRight) {
                    binary->Left = InsertImplicitCastIfNeeded(binary->Left, maybeStrRight.Cast(), &context);
                    type = maybeStrRight.Cast();
                    break;
                }
                // symbol + symbol => string
                if (maybeSymLeft && maybeSymRight) {
                    auto strT = std::make_shared<TStringType>();
                    binary->Left = InsertImplicitCastIfNeeded(binary->Left, strT, &context);
                    binary->Right = InsertImplicitCastIfNeeded(binary->Right, strT, &context);
                    type = strT;
                    break;
                }
            }

            auto common = CommonNumericType(left, right);
            if (!common) {
                if (auto result = TryModuleBinaryOp(binary->Left, binary->Right, binary->Operator, context)) {
                    co_return co_await AnnotateIfNeeded(result, context, scopeId);
                }
                co_return TError(binary->Location, "+, -, *, / применимы только к числам (оператор + также работает для строк)");
            }
            if (binary->Operator == TOperator("/")) {
                // 3/2 -> 1.5 : convert to float division
                common = std::make_shared<TFloatType>();
            }

            binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  common, &context);
            binary->Right = InsertImplicitCastIfNeeded(binary->Right, common, &context);
            type = common;
            break;
        }
        case TOperator("%"):
            // integer remainder
            if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both integer types");
            }
            break;
        case TOperator("^"):
            // power: left^right
            if (TMaybeType<TFloatType>(left) && TMaybeType<TIntegerType>(right)) {
                type = std::make_shared<TFloatType>();
            } else if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "binary expression operands must be both numeric types (float^int or int^int)");
            }
            break;
        case TOperator("&"):
        case TOperator("|"):
        case TOperator("xor"):
        case TOperator("<<"):
        case TOperator(">>"):
            if (TMaybeType<TIntegerType>(left) && TMaybeType<TIntegerType>(right)) {
                type = std::make_shared<TIntegerType>();
            } else {
                co_return TError(binary->Location, "Битовые операции применимы только к целым числам");
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
                    co_return TError(binary->Location, "Операции сравнения применимы только к числам");
                }
                binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  common, &context);
                binary->Right = InsertImplicitCastIfNeeded(binary->Right, common, &context);
            } else if (!(TMaybeType<TBoolType>(left) && TMaybeType<TBoolType>(right))) {
                if (auto result = TryModuleBinaryOp(binary->Left, binary->Right, binary->Operator, context)) {
                    co_return co_await AnnotateIfNeeded(result, context, scopeId);
                }
            }
            type = std::make_shared<TBoolType>();
            break;
        case TOperator("&&"):
        case TOperator("||"):
            binary->Left  = InsertImplicitCastIfNeeded(binary->Left,  std::make_shared<TBoolType>(), &context);
            binary->Right = InsertImplicitCastIfNeeded(binary->Right, std::make_shared<TBoolType>(), &context);
            type = std::make_shared<TBoolType>();
            break;
        default:
            if (auto result = TryModuleBinaryOp(binary->Left, binary->Right, binary->Operator, context)) {
                co_return co_await AnnotateIfNeeded(result, context, scopeId);
            }
            co_return TError(binary->Location, "Неизвестный бинарный оператор: '" + binary->Operator.ToString() + "'");
            break;
    }

    if (!type) {
        co_return TError(binary->Location, "Не удалось определить тип бинарного выражения");
    }

    binary->Type = type;
    if (explicitType && !EqualTypes(explicitType, type)) {
        co_return MakeCast(binary, explicitType);
    }
    if (explicitType) {
        binary->Type = explicitType;
    }
    co_return binary;
}

TTask AnnotateBlock(std::shared_ptr<TBlockExpr> block, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    for (auto& s : block->Stmts) {
        s = co_await DoAnnotate(s, context, NSemantics::TScopeId{block->Scope});
        if (!s->Type) {
            co_return TError(s->Location, "Не удалось определить тип выражения внутри блока");
        }
    }
    // Generic (template-parameterized) function declarations are blueprints,
    // not real functions — their bodies mention placeholder types and were
    // never body-annotated (see the HasTemplateParams guard in
    // AnnotateFunDecl). Only their monomorphized clones, synthesized at call
    // sites and registered separately, should reach lowering/codegen — drop
    // the templates themselves here so lowering never encounters an
    // unresolved `template` type.
    std::erase_if(block->Stmts, [](const TExprPtr& s) {
        auto fdecl = TMaybeNode<TFunDecl>(s);
        return fdecl && HasTemplateParams(*fdecl.Cast());
    });
    if (!block->Stmts.empty() && block->Stmts.back()->Type) {
        block->Type = block->Stmts.back()->Type;
    } else {
        block->Type = std::make_shared<TVoidType>();
    }
    co_return block;
}

TTask AnnotateSeq(std::shared_ptr<TSeqExpr> seq, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    for (auto& s : seq->Stmts) {
        s = co_await DoAnnotate(s, context, scopeId);
        if (!s->Type) {
            co_return TError(s->Location, "Не удалось определить тип выражения внутри последовательности");
        }
    }
    seq->Type = std::make_shared<TVoidType>();
    co_return seq;
}

TTask AnnotateAssign(std::shared_ptr<TAssignExpr> assign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    assign->Value = co_await DoAnnotate(assign->Value, context, scopeId);
    if (!assign->Value->Type) {
        co_return TError(assign->Location, "Нельзя присвоить значение с неопределённым типом переменной: " + assign->Name);
    }
    auto symbolId = context.Lookup(assign->Name, scopeId);
    if (!symbolId) {
        co_return TError(assign->Location, "Переменная не определена: " + assign->Name);
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym || !sym->Type) {
        co_return TError(assign->Location, "У переменной не определён тип: " + assign->Name);
    }
    auto symbolType = UnwrapReferenceType(sym->Type);

    if (!symbolType->Mutable) {
        co_return TError(assign->Location, "Нельзя присвоить аргументу функции '" + assign->Name + "'. Присваивать можно только переменным, а для аргументов функции — только если они объявлены как 'рез' или 'арг рез'.");
    }

    auto valueType = UnwrapReferenceType(assign->Value->Type);
    if (!EqualTypes(valueType, symbolType)) {
        auto futureValueType = FutureResultType(valueType);
        if (futureValueType && CanImplicit(futureValueType, symbolType, &context)) {
            assign->Type = std::make_shared<NAst::TVoidType>();
            co_return assign;
        }
        if (!CanImplicit(valueType, symbolType, &context)) {
            co_return TError(assign->Location, "Нельзя неявно преобразовать тип '" + std::string(valueType->TypeName()) + "' к типу '" + std::string(symbolType->TypeName()) + "' при присваивании переменной '" + assign->Name + "'.");
        }
        assign->Value = InsertImplicitCastIfNeeded(assign->Value, sym->Type, &context);
    }

    assign->Type = std::make_shared<NAst::TVoidType>();
    co_return assign;
}

TTask AnnotateArrayAssign(std::shared_ptr<TArrayAssignExpr> arrayAssign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    arrayAssign->Value = co_await DoAnnotate(arrayAssign->Value, context, scopeId);
    if (!arrayAssign->Value->Type) {
        co_return TError(arrayAssign->Location, "Нельзя присвоить значение без типа элементу массива '" + arrayAssign->Name + "'.");
    }
    auto symbolId = context.Lookup(arrayAssign->Name, scopeId);
    if (!symbolId) {
        co_return TError(arrayAssign->Location, "Массив не определён: '" + arrayAssign->Name + "'.");
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym || !sym->Type) {
        co_return TError(arrayAssign->Location, "У массива не определён тип: '" + arrayAssign->Name + "'.");
    }
    // Allow indexed assignment for arrays, strings and raw kernel pointers.
    auto maybeArrayType = TMaybeType<TArrayType>(sym->Type);
    auto maybeStringType = TMaybeType<TStringType>(sym->Type);
    auto maybePointerType = TMaybeType<TPointerType>(sym->Type);
    if (maybeArrayType) {
        auto arrayType = maybeArrayType.Cast();
        auto valueType = UnwrapReferenceType(arrayAssign->Value->Type);
        if (!EqualTypes(valueType, arrayType->ElementType)) {
            if (!CanImplicit(valueType, arrayType->ElementType, &context)) {
                co_return TError(arrayAssign->Location, "Нельзя неявно преобразовать тип '" +
                    std::string(valueType->TypeName()) + "' к типу '" + std::string(arrayType->ElementType->TypeName()) + "' при присваивании элементу массива '" + arrayAssign->Name + "'.");
            }
            arrayAssign->Value = InsertImplicitCastIfNeeded(arrayAssign->Value, arrayType->ElementType, &context);
        }
        for (auto& indexExpr : arrayAssign->Indices) {
            indexExpr = co_await DoAnnotate(indexExpr, context, scopeId);
            if (!indexExpr->Type) {
                co_return TError(arrayAssign->Location, "Индекс в присваивании элементу массива '" + arrayAssign->Name + "' не имеет типа. Убедитесь, что выражение индекса корректно и его тип определён.");
            }
            auto maybeIntType = TMaybeType<TIntegerType>(indexExpr->Type);
            if (!maybeIntType) {
                co_return TError(arrayAssign->Location, "Индекс в присваивании элементу массива '" + arrayAssign->Name + "' должен быть целым числом. Проверьте выражение индекса.");
            }
        }
        // check arity
        if (arrayAssign->Indices.size() != arrayType->Arity) {
            co_return TError(arrayAssign->Location, "Неверное количество индексов в присваивании элементу массива '" + arrayAssign->Name + "': ожидается " + std::to_string(arrayType->Arity) + ", получено " + std::to_string(arrayAssign->Indices.size()) + ".");
        }
        arrayAssign->Type = std::make_shared<NAst::TVoidType>();
        co_return arrayAssign;
    } else if (maybePointerType) {
        auto pointerType = maybePointerType.Cast();
        if (arrayAssign->Indices.size() != 1) {
            co_return TError(arrayAssign->Location, "В присваивании по указателю '" + arrayAssign->Name + "' должен быть ровно один индекс.");
        }

        arrayAssign->Indices[0] = co_await DoAnnotate(arrayAssign->Indices[0], context, scopeId);
        if (!arrayAssign->Indices[0]->Type) {
            co_return TError(arrayAssign->Location, "Индекс в присваивании по указателю '" + arrayAssign->Name + "' не имеет типа.");
        }
        auto intType = std::make_shared<TIntegerType>();
        if (!EqualTypes(arrayAssign->Indices[0]->Type, intType)) {
            if (!CanImplicit(arrayAssign->Indices[0]->Type, intType, &context)) {
                co_return TError(arrayAssign->Location, "Индекс в присваивании по указателю '" + arrayAssign->Name + "' должен быть целым числом.");
            }
            arrayAssign->Indices[0] = InsertImplicitCastIfNeeded(arrayAssign->Indices[0], intType, &context);
        }

        auto valueType = UnwrapReferenceType(arrayAssign->Value->Type);
        if (!EqualTypes(valueType, pointerType->PointeeType)) {
            if (!CanImplicit(valueType, pointerType->PointeeType, &context)) {
                co_return TError(arrayAssign->Location, "Нельзя неявно преобразовать тип '" +
                    std::string(valueType->TypeName()) + "' к типу '" + std::string(pointerType->PointeeType->TypeName()) + "' при присваивании по указателю '" + arrayAssign->Name + "'.");
            }
            arrayAssign->Value = InsertImplicitCastIfNeeded(arrayAssign->Value, pointerType->PointeeType, &context);
        }

        arrayAssign->Type = std::make_shared<NAst::TVoidType>();
        co_return arrayAssign;
    } else if (maybeStringType) {
        // Only allow s[1] = 'c' (symbol to string)
        if (arrayAssign->Indices.size() != 1) {
            co_return TError(arrayAssign->Location, "В присваивании элементу строки '" + arrayAssign->Name + "' должно быть ровно один индекс (например, s[1]='a'). Получено индексов: " + std::to_string(arrayAssign->Indices.size()) + ".");
        }
        // Index must be integer
        arrayAssign->Indices[0] = co_await DoAnnotate(arrayAssign->Indices[0], context, scopeId);
        if (!arrayAssign->Indices[0]->Type || !TMaybeType<TIntegerType>(arrayAssign->Indices[0]->Type)) {
            co_return TError(arrayAssign->Location, "Индекс в присваивании элементу строки '" + arrayAssign->Name + "' должен быть целым числом (например, s[1]='a').");
        }
        // Value must be symbol (char)
        auto valueType = UnwrapReferenceType(arrayAssign->Value->Type);
        if (!TMaybeType<TSymbolType>(valueType)) {
            co_return TError(arrayAssign->Location, "Значение в присваивании элементу строки '" + arrayAssign->Name + "' должно быть символом (например, s[1]='a').");
        }
        arrayAssign->Type = std::make_shared<NAst::TVoidType>();
        co_return arrayAssign;
    } else {
        co_return TError(arrayAssign->Location, "Идентификатор '" + arrayAssign->Name + "' не является массивом, строкой или указателем, поэтому нельзя использовать присваивание по индексу.");
    }
}

TTask AnnotateVar(std::shared_ptr<TVarStmt> var, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    if (!var->Type) {
        // (var name = expr): type inferred from init expression
        if (!var->Init) {
            co_return TError(var->Location, "Переменная без типа должна иметь инициализатор: " + var->Name);
        }
        var->Init = co_await DoAnnotate(var->Init, context, scopeId);
        if (!var->Init->Type) {
            co_return TError(var->Init->Location, "Не удалось определить тип инициализатора переменной: " + var->Name);
        }
        var->Type = UnwrapReferenceType(var->Init->Type);
        auto sidOpt = context.Lookup(var->Name, scopeId);
        if (sidOpt) {
            auto sym = context.GetSymbolNode(NSemantics::TSymbolId{sidOpt->Id});
            if (sym) { sym->Type = var->Type; }
        }
        co_return var;
    }
    for (auto& [from, to] : var->Bounds) {
        if (from) {
            from = co_await DoAnnotate(from, context, scopeId);
        }
        if (to) {
            to = co_await DoAnnotate(to, context, scopeId);
        }
    }
    co_return var;
}

TTask AnnotateIdent(std::shared_ptr<TIdentExpr> ident, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId, bool pathThrough = false) {
    auto symbolId = context.Lookup(ident->Name, scopeId);
    if (!symbolId) {
        co_return TError(ident->Location, "Идентификатор '" + ident->Name + "' не определён. Проверьте правильность написания имени или объявите переменную перед использованием.");
    }
    auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!sym) {
        co_return TError(ident->Location, "Внутренняя ошибка: не удалось получить информацию о символе идентификатора '" + ident->Name + "'.");
    }
    ident->Type = sym->Type;
    if (!ident->Type) {
        co_return TError(ident->Location, "У идентификатора '" + ident->Name + "' не определён тип. Возможно, переменная объявлена без типа или произошла ошибка при анализе типов.");
    }
    if (pathThrough) {
        co_return ident;
    }
    auto unwrappedType = UnwrapReferenceType(ident->Type);
    if (!unwrappedType->Readable) {
        co_return TError(ident->Location, "Нельзя читать из `рез' аргумента '" + ident->Name + "'.\n"
            "`рез' аргумент предназначен только для записи. Если нужно читать значение, объявите его как `арг рез'.");
    }
    if (auto maybeArray = TMaybeType<TArrayType>(unwrappedType)) {
        auto arrayType = maybeArray.Cast();
        if (!arrayType->ElementType->Readable) {
            co_return TError(ident->Location, "Нельзя читать элементы массива `рез' аргумента '" + ident->Name + "'.\n"
                "Массив `рез' аргумента предназначен только для записи. Если нужно читать элементы, объявите аргумент как `арг рез'.");
        }
    }

    co_return ident;
}

// --- Generic instantiation ---------------------------------------------------
//
// A `template` named-type placeholder ("<named K (template)>") in a function
// parameter marks the function as generic: at each call site the concrete
// types bound to its placeholders are inferred from argument types, and a
// monomorphized clone of the function is substituted, registered under a
// synthetic mangled name and cached by it.

bool ContainsTemplateParam(const TTypePtr& type) {
    if (!type) {
        return false;
    }
    if (auto named = TMaybeType<TNamedType>(type)) {
        return named.Cast()->Template;
    }
    if (auto arr = TMaybeType<TArrayType>(type)) {
        return ContainsTemplateParam(arr.Cast()->ElementType);
    }
    if (auto ptr = TMaybeType<TPointerType>(type)) {
        return ContainsTemplateParam(ptr.Cast()->PointeeType);
    }
    if (auto ref = TMaybeType<TReferenceType>(type)) {
        return ContainsTemplateParam(ref.Cast()->ReferencedType);
    }
    if (auto future = TMaybeType<TFutureType>(type)) {
        return ContainsTemplateParam(future.Cast()->ResultType);
    }
    if (auto fun = TMaybeType<TFunctionType>(type)) {
        auto f = fun.Cast();
        for (auto& p : f->ParamTypes) {
            if (ContainsTemplateParam(p)) {
                return true;
            }
        }
        return ContainsTemplateParam(f->ReturnType);
    }
    if (auto str = TMaybeType<TStructType>(type)) {
        for (auto& [_, fieldType] : str.Cast()->Fields) {
            if (ContainsTemplateParam(fieldType)) {
                return true;
            }
        }
    }
    return false;
}

bool HasTemplateParams(const TFunDecl& decl) {
    for (auto& param : decl.Params) {
        if (ContainsTemplateParam(param->Type)) {
            return true;
        }
    }
    return false;
}

// Collects names of `template` placeholders in first-seen order — defines the
// canonical order used both for mangled-name generation and for checking that
// every placeholder name got bound by unification.
void CollectTemplateParamNames(const TTypePtr& type, std::vector<std::string>& names) {
    if (!type) {
        return;
    }
    if (auto named = TMaybeType<TNamedType>(type)) {
        auto t = named.Cast();
        if (t->Template) {
            const auto& name = t->Name;
            for (auto& seen : names) {
                if (seen == name) return;
            }
            names.push_back(name);
        }
        return;
    }
    if (auto arr = TMaybeType<TArrayType>(type)) {
        CollectTemplateParamNames(arr.Cast()->ElementType, names);
        return;
    }
    if (auto ptr = TMaybeType<TPointerType>(type)) {
        CollectTemplateParamNames(ptr.Cast()->PointeeType, names);
        return;
    }
    if (auto ref = TMaybeType<TReferenceType>(type)) {
        CollectTemplateParamNames(ref.Cast()->ReferencedType, names);
        return;
    }
    if (auto future = TMaybeType<TFutureType>(type)) {
        CollectTemplateParamNames(future.Cast()->ResultType, names);
        return;
    }
    if (auto fun = TMaybeType<TFunctionType>(type)) {
        auto t = fun.Cast();
        for (auto& p : t->ParamTypes) {
            CollectTemplateParamNames(p, names);
        }
        CollectTemplateParamNames(t->ReturnType, names);
        return;
    }
    if (auto str = TMaybeType<TStructType>(type)) {
        for (auto& [_, fieldType] : str.Cast()->Fields) {
            CollectTemplateParamNames(fieldType, names);
        }
    }
}

// Rebuilds a fresh TType instance with the same derived "shape" (kind, name,
// nested types...) as `shape`, but default base TType attributes — the
// starting point for substituting a `template` placeholder: the result must
// structurally equal the bound concrete type, while the placeholder usage
// site's own attributes get overlaid on top (see SubstituteTemplateType).
TTypePtr CloneTypeShape(const TTypePtr& shape) {
    if (!shape) {
        return shape;
    }
    if (auto t = TMaybeType<TIntegerType>(shape)) {
        return std::make_shared<TIntegerType>(t.Cast()->Kind);
    }
    if (TMaybeType<TFloatType>(shape)) {
        return std::make_shared<TFloatType>();
    }
    if (TMaybeType<TBoolType>(shape)) {
        return std::make_shared<TBoolType>();
    }
    if (TMaybeType<TStringType>(shape)) {
        return std::make_shared<TStringType>();
    }
    if (TMaybeType<TSymbolType>(shape)) {
        return std::make_shared<TSymbolType>();
    }
    if (TMaybeType<TFileType>(shape)) {
        return std::make_shared<TFileType>();
    }
    if (TMaybeType<TVoidType>(shape)) {
        return std::make_shared<TVoidType>();
    }
    if (auto t = TMaybeType<TFunctionType>(shape)) {
        auto src = t.Cast();
        return std::make_shared<TFunctionType>(src->ParamTypes, src->ReturnType);
    }
    if (auto t = TMaybeType<TFutureType>(shape)) {
        return std::make_shared<TFutureType>(t.Cast()->ResultType);
    }
    if (auto t = TMaybeType<TArrayType>(shape)) {
        auto src = t.Cast();
        return std::make_shared<TArrayType>(src->ElementType, src->Arity);
    }
    if (auto t = TMaybeType<TPointerType>(shape)) {
        return std::make_shared<TPointerType>(t.Cast()->PointeeType);
    }
    if (auto t = TMaybeType<TReferenceType>(shape)) {
        return std::make_shared<TReferenceType>(t.Cast()->ReferencedType);
    }
    if (auto t = TMaybeType<TNamedType>(shape)) {
        auto src = t.Cast();
        auto result = std::make_shared<TNamedType>(src->Name, src->UnderlyingType);
        result->Reference = src->Reference;
        return result;
    }
    if (auto t = TMaybeType<TStructType>(shape)) {
        return std::make_shared<TStructType>(t.Cast()->Fields);
    }
    return shape;
}

// Replaces every `template` placeholder reachable in `type` with its bound
// concrete type from `bindings`, recursing into composite wrappers exactly
// like resolveTypeRef does (name_resolver.cpp:84-123). Preserves every TType
// attribute of the *placeholder usage site*: copies the whole base TType
// sub-object onto the cloned shape, then clears `Template` — so any present
// or future boolean attribute survives without this code needing to know its
// name.
TTypePtr SubstituteTemplateType(const TTypePtr& type, const std::map<std::string, TTypePtr>& bindings) {
    if (!type) {
        return type;
    }
    if (auto named = TMaybeType<TNamedType>(type)) {
        auto placeholder = named.Cast();
        if (!placeholder->Template) {
            return type;
        }
        auto it = bindings.find(placeholder->Name);
        if (it == bindings.end()) {
            return type; // left for the caller to detect (unbound generic parameter)
        }
        auto result = CloneTypeShape(it->second);
        static_cast<TType&>(*result) = static_cast<const TType&>(*placeholder);
        result->Template = false;
        return result;
    }
    if (auto arr = TMaybeType<TArrayType>(type)) {
        auto src = arr.Cast();
        auto elem = SubstituteTemplateType(src->ElementType, bindings);
        if (elem == src->ElementType) return type;
        auto result = std::make_shared<TArrayType>(std::move(elem), src->Arity);
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    if (auto ptr = TMaybeType<TPointerType>(type)) {
        auto src = ptr.Cast();
        auto pointee = SubstituteTemplateType(src->PointeeType, bindings);
        if (pointee == src->PointeeType) return type;
        auto result = std::make_shared<TPointerType>(std::move(pointee));
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    if (auto ref = TMaybeType<TReferenceType>(type)) {
        auto src = ref.Cast();
        auto referenced = SubstituteTemplateType(src->ReferencedType, bindings);
        if (referenced == src->ReferencedType) return type;
        auto result = std::make_shared<TReferenceType>(std::move(referenced));
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    if (auto future = TMaybeType<TFutureType>(type)) {
        auto src = future.Cast();
        auto inner = SubstituteTemplateType(src->ResultType, bindings);
        if (inner == src->ResultType) return type;
        auto result = std::make_shared<TFutureType>(std::move(inner));
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    if (auto fun = TMaybeType<TFunctionType>(type)) {
        auto src = fun.Cast();
        bool changed = false;
        std::vector<TTypePtr> params;
        params.reserve(src->ParamTypes.size());
        for (auto& p : src->ParamTypes) {
            auto sp = SubstituteTemplateType(p, bindings);
            changed = changed || (sp != p);
            params.push_back(std::move(sp));
        }
        auto ret = SubstituteTemplateType(src->ReturnType, bindings);
        changed = changed || (ret != src->ReturnType);
        if (!changed) return type;
        auto result = std::make_shared<TFunctionType>(std::move(params), std::move(ret));
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    if (auto str = TMaybeType<TStructType>(type)) {
        auto src = str.Cast();
        bool changed = false;
        std::vector<std::pair<std::string, TTypePtr>> fields;
        fields.reserve(src->Fields.size());
        for (auto& [name, fieldType] : src->Fields) {
            auto sf = SubstituteTemplateType(fieldType, bindings);
            changed = changed || (sf != fieldType);
            fields.emplace_back(name, std::move(sf));
        }
        if (!changed) return type;
        auto result = std::make_shared<TStructType>(std::move(fields));
        static_cast<TType&>(*result) = static_cast<const TType&>(*src);
        return result;
    }
    return type;
}

// Unifies a (possibly template-parameterized) declared parameter type against
// the concrete type of an actual argument, extending `bindings`. Returns an
// error message when the same placeholder name would have to bind to two
// structurally different concrete types.
std::optional<std::string> UnifyTemplateType(
    const TTypePtr& paramType,
    const TTypePtr& argType,
    std::map<std::string, TTypePtr>& bindings)
{
    if (!paramType || !argType) {
        return std::nullopt;
    }
    if (auto named = TMaybeType<TNamedType>(paramType)) {
        auto placeholder = named.Cast();
        if (!placeholder->Template) {
            return std::nullopt;
        }
        auto concrete = UnwrapReferenceType(argType);
        auto [it, inserted] = bindings.try_emplace(placeholder->Name, concrete);
        if (!inserted && TypeKey(it->second) != TypeKey(concrete)) {
            return "тип обобщённого параметра '" + placeholder->Name + "' определяется неоднозначно: то как '"
                + std::string(it->second->TypeName()) + "', то как '" + std::string(concrete->TypeName()) + "'";
        }
        return std::nullopt;
    }
    if (auto arr = TMaybeType<TArrayType>(paramType)) {
        if (auto argArr = TMaybeType<TArrayType>(argType)) {
            return UnifyTemplateType(arr.Cast()->ElementType, argArr.Cast()->ElementType, bindings);
        }
        return std::nullopt;
    }
    if (auto ptr = TMaybeType<TPointerType>(paramType)) {
        if (auto argPtr = TMaybeType<TPointerType>(argType)) {
            return UnifyTemplateType(ptr.Cast()->PointeeType, argPtr.Cast()->PointeeType, bindings);
        }
        return std::nullopt;
    }
    if (auto ref = TMaybeType<TReferenceType>(paramType)) {
        auto argRef = TMaybeType<TReferenceType>(argType);
        return UnifyTemplateType(ref.Cast()->ReferencedType, argRef ? argRef.Cast()->ReferencedType : argType, bindings);
    }
    if (auto future = TMaybeType<TFutureType>(paramType)) {
        if (auto argFuture = TMaybeType<TFutureType>(argType)) {
            return UnifyTemplateType(future.Cast()->ResultType, argFuture.Cast()->ResultType, bindings);
        }
        return std::nullopt;
    }
    if (auto fun = TMaybeType<TFunctionType>(paramType)) {
        auto argFun = TMaybeType<TFunctionType>(argType);
        if (!argFun) {
            return std::nullopt;
        }
        auto pf = fun.Cast();
        auto af = argFun.Cast();
        if (pf->ParamTypes.size() != af->ParamTypes.size()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < pf->ParamTypes.size(); ++i) {
            if (auto err = UnifyTemplateType(pf->ParamTypes[i], af->ParamTypes[i], bindings)) {
                return err;
            }
        }
        return UnifyTemplateType(pf->ReturnType, af->ReturnType, bindings);
    }
    if (auto str = TMaybeType<TStructType>(paramType)) {
        auto argStr = TMaybeType<TStructType>(argType);
        if (!argStr) {
            return std::nullopt;
        }
        auto ps = str.Cast();
        auto as = argStr.Cast();
        if (ps->Fields.size() != as->Fields.size()) {
            return std::nullopt;
        }
        for (size_t i = 0; i < ps->Fields.size(); ++i) {
            if (auto err = UnifyTemplateType(ps->Fields[i].second, as->Fields[i].second, bindings)) {
                return err;
            }
        }
    }
    return std::nullopt;
}

// Polymorphic shallow copy of one AST node (same scalar fields, shared child
// pointers) — dispatches on NodeName like the rest of this codebase
// (TMaybeNode), so we don't need a virtual Clone() across ~28 node types.
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
    if (auto n = TMaybeNode<TSeqExpr>(node)) {
        return std::make_shared<TSeqExpr>(*n.Cast());
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
    return node;
}

// Deep-clones an AST subtree, substituting `template` placeholders in every
// declared/annotated type along the way (TVarStmt::Type, TCastExpr's target
// type, ...) and resetting scope bookkeeping (TBlockExpr/TFunDecl/TLetExpr
// ::Scope, TLetExpr::TBinding::Symbol) so the clone gets fresh symbol-table
// entries on the next name-resolution pass: a cloned body cannot share scopes
// with its template — Symbols bind scope entries to specific AST node
// identities, and sharing would make lookups resolve to the template's
// (wrongly-typed) original nodes instead of this clone's.
TExprPtr CloneAndSubstituteExpr(const TExprPtr& node, const std::map<std::string, TTypePtr>& bindings) {
    if (!node) {
        return node;
    }
    auto clone = ShallowCloneNode(node);
    if (clone->Type) {
        clone->Type = SubstituteTemplateType(clone->Type, bindings);
    }
    if (auto block = TMaybeNode<TBlockExpr>(clone)) {
        block.Cast()->Scope = -1;
    } else if (auto fdecl = TMaybeNode<TFunDecl>(clone)) {
        fdecl.Cast()->Scope = -1;
    }
    for (auto* child : clone->MutableChildren()) {
        *child = CloneAndSubstituteExpr(*child, bindings);
    }
    return clone;
}

using TFunDeclTask = TExpectedTask<std::shared_ptr<TFunDecl>, TError, TLocation>;

// Clones+substitutes a generic TFunDecl for the concrete types inferred from
// `args`, registers the result under a synthetic mangled name and
// (re-)resolves+annotates its body. Caching by that name (via a root-scope
// symbol lookup — DeclareFunction inside ResolveInstantiatedFunDecl registers
// it there) both deduplicates repeat instantiations and guards against
// infinite recursion for mutually-recursive generics: the symbol is
// look-up-able before its body gets annotated, so a recursive call resolves
// to the already-registered (in-progress) clone instead of re-entering.
TFunDeclTask InstantiateGenericFunction(
    std::shared_ptr<TFunDecl> templateDecl,
    const std::vector<TExprPtr>& args,
    NSemantics::TNameResolver& context,
    TLocation callLoc)
{
    std::map<std::string, TTypePtr> bindings;
    for (size_t i = 0; i < templateDecl->Params.size() && i < args.size(); ++i) {
        if (auto err = UnifyTemplateType(templateDecl->Params[i]->Type, args[i]->Type, bindings)) {
            co_return TError(callLoc, "Не удалось вывести типы обобщённых параметров функции '" + templateDecl->Name + "': " + *err);
        }
    }

    std::vector<std::string> paramNames;
    for (auto& param : templateDecl->Params) {
        CollectTemplateParamNames(param->Type, paramNames);
    }
    CollectTemplateParamNames(templateDecl->RetType, paramNames);
    for (auto& name : paramNames) {
        if (!bindings.contains(name)) {
            co_return TError(callLoc, "Не удалось определить тип обобщённого параметра '" + name +
                "' функции '" + templateDecl->Name + "' по аргументам вызова.");
        }
    }

    std::string mangledName = "__generic_" + templateDecl->Name;
    for (auto& name : paramNames) {
        mangledName += "$" + TypeKey(bindings[name]);
    }

    auto rootScopeId = context.GetOrCreateRootScope()->Id;
    if (auto found = context.Lookup(mangledName, rootScopeId)) {
        if (auto cached = TMaybeNode<TFunDecl>(context.GetSymbolNode(NSemantics::TSymbolId{found->Id})).Cast()) {
            co_return cached;
        }
    }

    std::vector<TParam> params;
    params.reserve(templateDecl->Params.size());
    for (auto& param : templateDecl->Params) {
        auto clonedParam = std::make_shared<TVarStmt>(*param);
        clonedParam->Type = SubstituteTemplateType(param->Type, bindings);
        for (auto& bound : clonedParam->Bounds) {
            bound.first = CloneAndSubstituteExpr(bound.first, bindings);
            bound.second = CloneAndSubstituteExpr(bound.second, bindings);
        }
        params.push_back(std::move(clonedParam));
    }
    auto retType = SubstituteTemplateType(templateDecl->RetType, bindings);

    if (!templateDecl->Body) {
        // Generic external/built-in function: no AST body to clone, just a
        // concrete signature wrapping the same native implementation
        // (Ptr/Packed/MangledName/InlineFactory carried over unchanged —
        // type-dependent natives are expected to branch on the InlineFactory's
        // already-annotated, concretely-typed call arguments). Registered
        // directly via DeclareFunction so it's picked up by
        // GetExternalFunctions/ImportExternalFunctions like any other builtin.
        std::vector<TTypePtr> externParamTypes;
        externParamTypes.reserve(params.size());
        for (auto& param : params) {
            externParamTypes.push_back(param->Type);
        }
        auto cloned = std::make_shared<TFunDecl>(templateDecl->Location, mangledName, std::move(params), nullptr, retType);
        cloned->MangledName = templateDecl->MangledName;
        cloned->Ptr = templateDecl->Ptr;
        cloned->Packed = templateDecl->Packed;
        cloned->RequireArgsMaterialization = templateDecl->RequireArgsMaterialization;
        cloned->InlineFactory = templateDecl->InlineFactory;
        cloned->Type = std::make_shared<TFunctionType>(std::move(externParamTypes), cloned->RetType);

        context.DeclareFunction(mangledName, cloned);
        co_return cloned;
    }

    auto body = TMaybeNode<TBlockExpr>(CloneAndSubstituteExpr(templateDecl->Body, bindings)).Cast();

    auto cloned = std::make_shared<TFunDecl>(templateDecl->Location, mangledName, std::move(params), body, retType);
    cloned->LastAssert = CloneAndSubstituteExpr(templateDecl->LastAssert, bindings);

    std::vector<TTypePtr> paramTypes;
    paramTypes.reserve(cloned->Params.size());
    for (auto& param : cloned->Params) {
        paramTypes.push_back(param->Type);
    }
    cloned->Type = std::make_shared<TFunctionType>(std::move(paramTypes), cloned->RetType);

    auto declRes = context.ResolveInstantiatedFunDecl(cloned);
    if (!declRes) {
        co_return declRes.error();
    }

    co_await DoAnnotate(cloned->Body, context, NSemantics::TScopeId{cloned->Scope});
    if (!cloned->Body->Type) {
        co_return TError(callLoc, "Не удалось определить тип тела инстанциированной функции '" + templateDecl->Name + "'");
    }

    co_return cloned;
}

TTask AnnotateOverloadedCall(
    std::shared_ptr<TCallExpr> call,
    const std::vector<NSemantics::TSymbolId>& overloads,
    NSemantics::TNameResolver& context,
    NSemantics::TScopeId scopeId)
{
    for (auto& arg : call->Args) {
        arg = co_await DoAnnotate(arg, context, scopeId);
        if (!arg->Type) {
            co_return TError(arg->Location, "argument has no type");
        }
    }

    int bestCost = std::numeric_limits<int>::max();
    std::shared_ptr<TFunDecl> bestDecl;
    bool ambiguous = false;

    for (const auto& symId : overloads) {
        auto funDecl = TMaybeNode<TFunDecl>(context.GetSymbolNode(symId)).Cast();
        if (!funDecl || funDecl->Params.size() != call->Args.size()) {
            continue;
        }
        int totalCost = 0;
        bool viable = true;
        for (size_t i = 0; i < call->Args.size(); ++i) {
            auto cost = ArgCost(call->Args[i]->Type, funDecl->Params[i]->Type, &context);
            if (!cost) {
                viable = false;
                break;
            }
            totalCost += *cost;
        }
        if (!viable) {
            continue;
        }
        if (totalCost < bestCost) {
            bestCost = totalCost;
            bestDecl = funDecl;
            ambiguous = false;
        } else if (totalCost == bestCost) {
            ambiguous = true;
        }
    }

    if (!bestDecl) {
        co_return TError(call->Location, "no matching overload for '" + TMaybeNode<TIdentExpr>(call->Callee).Cast()->Name + "'");
    }
    if (ambiguous) {
        co_return TError(call->Location, "ambiguous overload for '" + TMaybeNode<TIdentExpr>(call->Callee).Cast()->Name + "'");
    }

    if (HasTemplateParams(*bestDecl)) {
        auto instantiated = co_await InstantiateGenericFunction(bestDecl, call->Args, context, call->Location);
        bestDecl = instantiated;
    }

    for (size_t i = 0; i < call->Args.size(); ++i) {
        call->Args[i] = InsertImplicitCastIfNeeded(call->Args[i], bestDecl->Params[i]->Type, &context);
    }

    auto callee = std::make_shared<TIdentExpr>(call->Callee->Location, bestDecl->Name);
    callee->Type = bestDecl->Type;
    call->Callee = callee;
    call->Type = bestDecl->RetType;

    if (bestDecl->InlineFactory) {
        auto inlined = (*bestDecl->InlineFactory)(call->Args);
        if (inlined) {
            co_return co_await DoAnnotate(inlined, context, scopeId);
        }
    }

    co_return call;
}

TTask AnnotateCall(std::shared_ptr<TCallExpr> call, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    if (auto maybeIdent = TMaybeNode<TIdentExpr>(call->Callee)) {
        auto overloads = context.LookupOverloads(maybeIdent.Cast()->Name, scopeId);
        if (overloads.size() > 1) {
            co_return co_await AnnotateOverloadedCall(call, overloads, context, scopeId);
        }
        // The single candidate has `template` parameters — instantiate it
        // for the call's argument types and re-target the callee at the
        // synthesized clone, registered in the root scope under a mangled
        // name that ordinary identifier resolution can find.
        if (overloads.size() == 1) {
            auto templateDecl = TMaybeNode<TFunDecl>(context.GetSymbolNode(overloads[0])).Cast();
            if (templateDecl && HasTemplateParams(*templateDecl)) {
                for (auto& arg : call->Args) {
                    arg = co_await DoAnnotate(arg, context, scopeId);
                    if (!arg->Type) {
                        co_return TError(arg->Location, "argument has no type");
                    }
                }
                if (templateDecl->Params.size() != call->Args.size()) {
                    co_return TError(call->Location, "Неверное количество аргументов при вызове функции '" + templateDecl->Name +
                        "': ожидается " + std::to_string(templateDecl->Params.size()) + ", передано " + std::to_string(call->Args.size()) + ".");
                }
                auto instantiated = co_await InstantiateGenericFunction(templateDecl, call->Args, context, call->Location);
                call->Callee = std::make_shared<TIdentExpr>(call->Callee->Location, instantiated->Name);
            }
        }
    }

    call->Callee = co_await DoAnnotate(call->Callee, context, scopeId);
    if (!call->Callee->Type) {
        co_return TError(call->Location, "Нельзя вызвать выражение без типа (функция или процедура не определена или не имеет типа).");
    }
    auto maybeFunType = TMaybeType<TFunctionType>(call->Callee->Type);
    if (maybeFunType) {
        auto funT = maybeFunType.Cast();
        if (funT->ParamTypes.size() != call->Args.size()) {
            co_return TError(call->Location, "Неверное количество аргументов при вызове функции: ожидается " + std::to_string(funT->ParamTypes.size()) + ", передано " + std::to_string(call->Args.size()) + ".");
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
                co_return TError(arg->Location, "Аргумент #" + std::to_string(i+1) + " не имеет типа. Проверьте корректность выражения.");
            }

            // if ParamT is reference type, arg must be of the same referenced type or ident of that underlying type
            if (maybeRef) {
                auto paramRefT = maybeRef.Cast();
                if (maybeIdent) {
                    auto identType = UnwrapReferenceType(maybeIdent.Cast()->Type);
                    // ident must be writable
                    if (maybeIdent && !identType->Mutable) {
                        co_return TError(arg->Location,
                            "Аргумент #" + std::to_string(i+1) + ": нельзя передать этот идентификатор в ссылочный (рез/арг рез) параметр, так как он не является изменяемым (immutable).\n"
                            "\nПричина: вы передаёте, например, аргумент функции, объявленный как обычный, в параметр, объявленный как 'рез' или 'арг рез'.\n"
                            "\nРешения:\n"
                            "- Объявите параметр исходной функции как 'арг рез', если хотите передавать его по ссылке и изменять.\n"
                            "- Или скопируйте значение в отдельную переменную и передайте её.\n"
                            "\nПример:\n"
                            "алг ф(цел i)\n  g(i)  // ошибка: i нельзя передать как 'рез'\n\nалг g(рез i)\n  ...\n");
                    }
                }
                auto argTypeUnwrapped = UnwrapReferenceType(arg->Type);
                if (!EqualTypes(argTypeUnwrapped, paramRefT->ReferencedType)) {
                    co_return TError(arg->Location, "Аргумент #" + std::to_string(i+1) + ": тип '" + std::string(arg->Type->TypeName()) + "' не совпадает с типом ссылочного параметра '" + std::string(paramT->TypeName()) + "'.");
                }
                // no implicit cast for reference parameters
                continue;
            }
            if (!EqualTypes(arg->Type, paramT)) {
                if (!CanImplicit(UnwrapReferenceType(arg->Type), paramT, &context)) {
                    co_return TError(arg->Location, "Аргумент #" + std::to_string(i+1) + ": нельзя неявно преобразовать тип '" + std::string(arg->Type->TypeName()) + "' к типу параметра '" + std::string(paramT->TypeName()) + "'.");
                }
                arg = InsertImplicitCastIfNeeded(arg, paramT, &context);
            }
        }
        call->Type = maybeFunType.Cast()->ReturnType;

    } else {
        call->Type = call->Callee->Type;
    }
    co_return call;
}

TTask AnnotateAwait(std::shared_ptr<TAwaitExpr> awaitExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    awaitExpr->Operand = co_await DoAnnotate(awaitExpr->Operand, context, scopeId);
    if (!awaitExpr->Operand->Type) {
        co_return TError(awaitExpr->Location, "Нельзя ожидать выражение без типа.");
    }

    auto resultType = FutureResultType(awaitExpr->Operand->Type);
    if (!resultType) {
        co_return TError(awaitExpr->Location, "Нельзя ожидать результат выражения, которое не возвращает Future.");
    }

    awaitExpr->Type = resultType;
    co_return awaitExpr;
}

TTask AnnotateIfExpr(std::shared_ptr<TIfExpr> ifExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    ifExpr->Cond = co_await DoAnnotate(ifExpr->Cond, context, scopeId);
    if (!ifExpr->Cond->Type) {
        co_return TError(ifExpr->Cond->Location, "Условие в `если' не имеет типа. Проверьте корректность выражения в условии.");
    }
    auto boolT = std::make_shared<TBoolType>();
    auto condType = UnwrapReferenceType(ifExpr->Cond->Type);
    if (!EqualTypes(condType, boolT)) {
        if (!CanImplicit(condType, boolT, &context)) {
            co_return TError(ifExpr->Cond->Location, "Условие в `если' должно иметь логический тип или приводиться к нему.");
        }
        ifExpr->Cond = InsertImplicitCastIfNeeded(ifExpr->Cond, boolT, &context);
    }

    ifExpr->Then = co_await DoAnnotate(ifExpr->Then, context, scopeId);
    if (!ifExpr->Then->Type) {
        co_return TError(ifExpr->Then->Location, "Ветвь `то' в `если' не имеет типа.");
    }

    if (!ifExpr->Else) {
        // No else branch: then must be void, overall type is void
        auto thenType = UnwrapReferenceType(ifExpr->Then->Type);
        if (!TMaybeType<TVoidType>(thenType)) {
            co_return TError(ifExpr->Then->Location, "if-expression без ветви `иначе': ветвь `то' должна иметь тип void.");
        }
        ifExpr->Type = std::make_shared<TVoidType>();
        co_return ifExpr;
    }

    ifExpr->Else = co_await DoAnnotate(ifExpr->Else, context, scopeId);
    if (!ifExpr->Else->Type) {
        co_return TError(ifExpr->Else->Location, "Ветвь `иначе' в `если' не имеет типа.");
    }

    auto thenType = UnwrapReferenceType(ifExpr->Then->Type);
    auto elseType = UnwrapReferenceType(ifExpr->Else->Type);

    // Both branches void → statement context
    if (TMaybeType<TVoidType>(thenType) && TMaybeType<TVoidType>(elseType)) {
        ifExpr->Type = std::make_shared<TVoidType>();
        co_return ifExpr;
    }

    // Expression context: both branches must yield a compatible non-void type
    if (TMaybeType<TVoidType>(thenType) || TMaybeType<TVoidType>(elseType)) {
        co_return TError(ifExpr->Location, "if-expression: ветви должны иметь одинаковый не-void тип.");
    }
    auto common = CommonValueType(thenType, elseType, context);
    if (!common) {
        co_return TError(ifExpr->Location,
            "if-expression: типы ветвей несовместимы: '" + std::string(thenType->TypeName()) +
            "' и '" + std::string(elseType->TypeName()) + "'.");
    }
    ifExpr->Then = InsertImplicitCastIfNeeded(ifExpr->Then, common, &context);
    ifExpr->Else = InsertImplicitCastIfNeeded(ifExpr->Else, common, &context);
    ifExpr->Type = common;
    co_return ifExpr;
}

TTask AnnotateWhile(std::shared_ptr<TWhileStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    loop->Cond = co_await DoAnnotate(loop->Cond, context, scopeId);
    loop->Cond = InsertImplicitCastIfNeeded(loop->Cond, std::make_shared<TBoolType>(), &context);
    loop->Body = co_await DoAnnotate(loop->Body, context, scopeId);
    loop->Type = std::make_shared<TVoidType>();
    co_return loop;
}

TTask AnnotateRepeat(std::shared_ptr<TRepeatStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    loop->Body = co_await DoAnnotate(loop->Body, context, scopeId);
    loop->Cond = co_await DoAnnotate(loop->Cond, context, scopeId);
    loop->Cond = InsertImplicitCastIfNeeded(loop->Cond, std::make_shared<TBoolType>(), &context);
    loop->Type = std::make_shared<TVoidType>();
    co_return loop;
}

TTask AnnotateFor(std::shared_ptr<TForStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    auto intType = std::make_shared<TIntegerType>();

    auto symbolId = context.Lookup(loop->VarName, scopeId);
    if (!symbolId) {
        co_return TError(loop->Location, "Переменная цикла не определена: " + loop->VarName);
    }
    auto symbol = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
    if (!symbol || !TMaybeType<TIntegerType>(symbol->Type)) {
        co_return TError(loop->Location, "Переменная цикла должна иметь целый тип: " + loop->VarName);
    }

    loop->From = co_await DoAnnotate(loop->From, context, scopeId);
    loop->From = InsertImplicitCastIfNeeded(loop->From, intType, &context);
    loop->To = co_await DoAnnotate(loop->To, context, scopeId);
    loop->To = InsertImplicitCastIfNeeded(loop->To, intType, &context);
    if (loop->Step) {
        loop->Step = co_await DoAnnotate(loop->Step, context, scopeId);
        loop->Step = InsertImplicitCastIfNeeded(loop->Step, intType, &context);
    }
    loop->Body = co_await DoAnnotate(loop->Body, context, scopeId);
    loop->Type = std::make_shared<TVoidType>();
    co_return loop;
}

TTask AnnotateTimes(std::shared_ptr<TTimesStmtExpr> loop, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    loop->Count = co_await DoAnnotate(loop->Count, context, scopeId);
    loop->Count = InsertImplicitCastIfNeeded(loop->Count, std::make_shared<TIntegerType>(), &context);
    loop->Body = co_await DoAnnotate(loop->Body, context, scopeId);
    loop->Type = std::make_shared<TVoidType>();
    co_return loop;
}

TTask AnnotateMultiIndex(std::shared_ptr<TMultiIndexExpr> indexExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    indexExpr->Collection = co_await DoAnnotate(indexExpr->Collection, context, scopeId);
    if (!indexExpr->Collection->Type) {
        co_return TError(indexExpr->Location, "В выражении многомерного индекса не удалось определить тип коллекции (массив). Проверьте корректность выражения.");
    }
    auto maybeArrayType = TMaybeType<TArrayType>(indexExpr->Collection->Type);
    if (!maybeArrayType) {
        co_return TError(indexExpr->Location, "Многомерная индексация поддерживается только для массивов. Проверьте, что вы обращаетесь к массиву, а не к другому типу.");
    }

    auto intType = std::make_shared<TIntegerType>();
    for (size_t i = 0; i < indexExpr->Indices.size(); ++i) {
        auto& index = indexExpr->Indices[i];
        index = co_await DoAnnotate(index, context, scopeId);
        if (!index->Type) {
            co_return TError(index->Location, "Индекс #" + std::to_string(i+1) + " в многомерном обращении к массиву не имеет типа. Проверьте выражение индекса.");
        }
        if (!EqualTypes(index->Type, intType)) {
            if (!CanImplicit(index->Type, intType, &context)) {
                co_return TError(index->Location, "Индекс #" + std::to_string(i+1) + " в многомерном обращении к массиву должен быть целым числом.");
            }
            index = InsertImplicitCastIfNeeded(index, intType, &context);
        }
    }
    auto arrayType = maybeArrayType.Cast();
    indexExpr->Type = arrayType->ElementType;

    co_return indexExpr;
}

TTask AnnotateIndex(std::shared_ptr<TIndexExpr> indexExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    indexExpr->Collection = co_await DoAnnotate(indexExpr->Collection, context, scopeId);
    if (!indexExpr->Collection->Type) {
        co_return TError(indexExpr->Location, "В выражении индексации не удалось определить тип коллекции (массива или строки). Проверьте корректность выражения.");
    }
    indexExpr->Index = co_await DoAnnotate(indexExpr->Index, context, scopeId);
    if (!indexExpr->Index->Type) {
        co_return TError(indexExpr->Location, "Индекс в выражении индексации не имеет типа. Убедитесь, что выражение индекса корректно и его тип определён.");
    }
    auto intType = std::make_shared<TIntegerType>();
    if (!EqualTypes(indexExpr->Index->Type, intType)) {
        if (!CanImplicit(indexExpr->Index->Type, intType, &context)) {
            co_return TError(indexExpr->Location, "Индекс в выражении индексации должен быть целым числом. Например: a[2], s[1].");
        }
        indexExpr->Index = InsertImplicitCastIfNeeded(indexExpr->Index, intType, &context);
    }
    auto collectionType = UnwrapReferenceType(indexExpr->Collection->Type);
    if (TMaybeType<TStringType>(collectionType)) {
        indexExpr->Type = std::make_shared<TSymbolType>();
    } else if (auto maybeArrayType = TMaybeType<TArrayType>(collectionType)) {
        auto arrayType = maybeArrayType.Cast();
        indexExpr->Type = arrayType->ElementType;
    } else if (auto maybePointerType = TMaybeType<TPointerType>(collectionType)) {
        indexExpr->Type = maybePointerType.Cast()->PointeeType;
    } else {
        co_return TError(indexExpr->Location, "Индексация поддерживается только для массивов, строк и указателей.\n"
            "Пример корректной индексации массива: a[2] (где a — массив).\n"
            "Пример корректной индексации строки: s[1] (где s — строка).\n"
            "Проверьте, что вы обращаетесь к массиву, строке или указателю, а не к другому типу.");
    }

    co_return indexExpr;
}

TTask AnnotateSlice(std::shared_ptr<TSliceExpr> sliceExpr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    sliceExpr->Collection = co_await DoAnnotate(sliceExpr->Collection, context, scopeId);
    if (!sliceExpr->Collection->Type) {
        co_return TError(sliceExpr->Location, "В выражении среза не удалось определить тип коллекции. Проверьте корректность выражения.");
    }
    auto collectionType = UnwrapReferenceType(sliceExpr->Collection->Type);
    if (!TMaybeType<TStringType>(collectionType)) {
        co_return TError(sliceExpr->Location, "Срезы поддерживаются только для строк.\n"
            "Пример корректного среза: s[1:3] (где s — строка).\n"
            "Проверьте, что вы используете строку, а не другой тип.");
    }
    sliceExpr->Start = co_await DoAnnotate(sliceExpr->Start, context, scopeId);
    if (!sliceExpr->Start->Type) {
        co_return TError(sliceExpr->Location, "Начальный индекс в срезе не имеет типа. Убедитесь, что выражение индекса корректно и его тип определён.");
    }
    sliceExpr->End = co_await DoAnnotate(sliceExpr->End, context, scopeId);
    if (!sliceExpr->End->Type) {
        co_return TError(sliceExpr->Location, "Конечный индекс в срезе не имеет типа. Убедитесь, что выражение индекса корректно и его тип определён.");
    }
    auto intType = std::make_shared<TIntegerType>();
    if (!EqualTypes(sliceExpr->Start->Type, intType)) {
        if (!CanImplicit(sliceExpr->Start->Type, intType, &context)) {
            co_return TError(sliceExpr->Location, "Начальный индекс в срезе должен быть целым числом. Пример: s[1:3].");
        }
        sliceExpr->Start = InsertImplicitCastIfNeeded(sliceExpr->Start, intType, &context);
    }
    if (!EqualTypes(sliceExpr->End->Type, intType)) {
        if (!CanImplicit(sliceExpr->End->Type, intType, &context)) {
            co_return TError(sliceExpr->Location, "Конечный индекс в срезе должен быть целым числом. Пример: s[1:3].");
        }
        sliceExpr->End = InsertImplicitCastIfNeeded(sliceExpr->End, intType, &context);
    }
    // Срез строки возвращает строку
    sliceExpr->Type = collectionType;

    co_return sliceExpr;
}

TTask AnnotateFieldAccess(std::shared_ptr<TFieldAccessExpr> fieldAccess, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    fieldAccess->Object = co_await DoAnnotate(fieldAccess->Object, context, scopeId);
    if (!fieldAccess->Object->Type) {
        co_return TError(fieldAccess->Location, "Не удалось определить тип объекта в выражении обращения к полю '" + fieldAccess->FieldName + "'.");
    }

    auto objType = UnwrapNamedType(UnwrapReferenceType(fieldAccess->Object->Type));
    auto maybeStruct = TMaybeType<TStructType>(objType);
    if (!maybeStruct) {
        co_return TError(fieldAccess->Location,
            "Обращение к полю '" + fieldAccess->FieldName + "' невозможно: объект не является структурой (тип: " + std::string(fieldAccess->Object->Type->TypeName()) + ").");
    }
    auto structType = maybeStruct.Cast();

    const auto& fields = structType->Fields;
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        if (fields[i].first == fieldAccess->FieldName) {
            fieldAccess->FieldIndex = i;
            fieldAccess->Type = fields[i].second;
            co_return fieldAccess;
        }
    }

    std::string available;
    for (const auto& [name, _] : fields) {
        if (!available.empty()) available += ", ";
        available += name;
    }
    co_return TError(fieldAccess->Location,
        "Поле '" + fieldAccess->FieldName + "' не найдено в структуре. Доступные поля: " + available + ".");
}

static std::pair<std::shared_ptr<TStructType>, std::string>
ResolveStructField(TTypePtr objType, const std::string& fieldName, int& outIndex) {
    auto structType = TMaybeType<TStructType>(UnwrapNamedType(UnwrapReferenceType(objType))).Cast();
    if (!structType) return {nullptr, ""};
    const auto& fields = structType->Fields;
    for (int i = 0; i < static_cast<int>(fields.size()); ++i) {
        if (fields[i].first == fieldName) {
            outIndex = i;
            return {structType, ""};
        }
    }
    std::string available;
    for (const auto& [name, _] : fields) {
        if (!available.empty()) available += ", ";
        available += name;
    }
    return {nullptr, available};
}

TTask AnnotateFieldAssign(std::shared_ptr<TFieldAssignExpr> fieldAssign, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    fieldAssign->Object = co_await DoAnnotate(fieldAssign->Object, context, scopeId);
    if (!fieldAssign->Object->Type) {
        co_return TError(fieldAssign->Location, "Не удалось определить тип объекта при присваивании полю '" + fieldAssign->FieldName + "'.");
    }

    int fieldIdx = -1;
    auto [structType, available] = ResolveStructField(fieldAssign->Object->Type, fieldAssign->FieldName, fieldIdx);
    if (!structType) {
        if (available.empty()) {
            co_return TError(fieldAssign->Location,
                "Присваивание полю '" + fieldAssign->FieldName + "' невозможно: объект не является структурой.");
        }
        co_return TError(fieldAssign->Location,
            "Поле '" + fieldAssign->FieldName + "' не найдено в структуре. Доступные поля: " + available + ".");
    }
    fieldAssign->FieldIndex = fieldIdx;
    auto fieldType = structType->Fields[fieldIdx].second;

    fieldAssign->Value = co_await DoAnnotate(fieldAssign->Value, context, scopeId);
    if (!fieldAssign->Value->Type) {
        co_return TError(fieldAssign->Location, "Не удалось определить тип значения при присваивании полю '" + fieldAssign->FieldName + "'.");
    }
    auto valueType = UnwrapReferenceType(fieldAssign->Value->Type);
    if (!EqualTypes(valueType, fieldType)) {
        if (!CanImplicit(valueType, fieldType, &context)) {
            co_return TError(fieldAssign->Location,
                "Нельзя неявно преобразовать тип '" + std::string(valueType->TypeName()) +
                "' к типу поля '" + fieldAssign->FieldName + "' (" + std::string(fieldType->TypeName()) + ").");
        }
        fieldAssign->Value = InsertImplicitCastIfNeeded(fieldAssign->Value, fieldType, &context);
    }

    fieldAssign->Type = std::make_shared<TVoidType>();
    co_return fieldAssign;
}

TTask DoAnnotate(TExprPtr expr, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    if (auto maybeBinary = TMaybeNode<TBinaryExpr>(expr)) {
        co_return co_await AnnotateBinary(maybeBinary.Cast(), context, scopeId);
    } else if (auto maybeNum = TMaybeNode<TNumberExpr>(expr)) {
        co_return AnnotateNumber(maybeNum.Cast());
    } else if (auto maybeUnary = TMaybeNode<TUnaryExpr>(expr)) {
        co_return co_await AnnotateUnary(maybeUnary.Cast(), context, scopeId);
    } else if (auto maybeBlock = TMaybeNode<TBlockExpr>(expr)) {
        co_return co_await AnnotateBlock(maybeBlock.Cast(), context, scopeId);
    } else if (auto maybeSeq = TMaybeNode<TSeqExpr>(expr)) {
        co_return co_await AnnotateSeq(maybeSeq.Cast(), context, scopeId);
    } else if (auto maybeIdent = TMaybeNode<TIdentExpr>(expr)) {
        co_return co_await AnnotateIdent(maybeIdent.Cast(), context, scopeId);
    } else if (auto maybeAssign = TMaybeNode<TAssignExpr>(expr)) {
        co_return co_await AnnotateAssign(maybeAssign.Cast(), context, scopeId);
    } else if (auto maybeArrayAssign = TMaybeNode<TArrayAssignExpr>(expr)) {
        co_return co_await AnnotateArrayAssign(maybeArrayAssign.Cast(), context, scopeId);
    } else if (auto maybeMultiIndex = TMaybeNode<TMultiIndexExpr>(expr)) {
        co_return co_await AnnotateMultiIndex(maybeMultiIndex.Cast(), context, scopeId);
    } else if (auto maybeVar = TMaybeNode<TVarStmt>(expr)) {
        co_return co_await AnnotateVar(maybeVar.Cast(), context, scopeId);
    } else if (auto maybeFunDecl = TMaybeNode<TFunDecl>(expr)) {
        co_return co_await AnnotateFunDecl(maybeFunDecl.Cast(), context, scopeId);
    } else if (auto maybeCall = TMaybeNode<TCallExpr>(expr)) {
        co_return co_await AnnotateCall(maybeCall.Cast(), context, scopeId);
    } else if (auto maybeAwait = TMaybeNode<TAwaitExpr>(expr)) {
        co_return co_await AnnotateAwait(maybeAwait.Cast(), context, scopeId);
    } else if (auto maybeIf = TMaybeNode<TIfExpr>(expr)) {
        co_return co_await AnnotateIfExpr(maybeIf.Cast(), context, scopeId);
    } else if (auto maybeIndex = TMaybeNode<TIndexExpr>(expr)) {
        co_return co_await AnnotateIndex(maybeIndex.Cast(), context, scopeId);
    } else if (auto maybeSlice = TMaybeNode<TSliceExpr>(expr)) {
        co_return co_await AnnotateSlice(maybeSlice.Cast(), context, scopeId);
    } else if (auto maybeFieldAccess = TMaybeNode<TFieldAccessExpr>(expr)) {
        co_return co_await AnnotateFieldAccess(maybeFieldAccess.Cast(), context, scopeId);
    } else if (auto maybeFieldAssign = TMaybeNode<TFieldAssignExpr>(expr)) {
        co_return co_await AnnotateFieldAssign(maybeFieldAssign.Cast(), context, scopeId);
    } else if (TMaybeNode<TUseExpr>(expr)) {
        expr->Type = std::make_shared<TVoidType>();
        co_return expr;
    } else if (TMaybeNode<TBreakStmt>(expr)) {
        expr->Type = std::make_shared<TVoidType>();
        co_return expr;
    } else if (TMaybeNode<TContinueStmt>(expr)) {
        expr->Type = std::make_shared<TVoidType>();
        co_return expr;
    } else if (auto maybeLoop = TMaybeNode<TWhileStmtExpr>(expr)) {
        co_return co_await AnnotateWhile(maybeLoop.Cast(), context, scopeId);
    } else if (auto maybeLoop = TMaybeNode<TRepeatStmtExpr>(expr)) {
        co_return co_await AnnotateRepeat(maybeLoop.Cast(), context, scopeId);
    } else if (auto maybeLoop = TMaybeNode<TForStmtExpr>(expr)) {
        co_return co_await AnnotateFor(maybeLoop.Cast(), context, scopeId);
    } else if (auto maybeLoop = TMaybeNode<TTimesStmtExpr>(expr)) {
        co_return co_await AnnotateTimes(maybeLoop.Cast(), context, scopeId);
    } else {
        if (!expr->Type) {
            // if expr->Type => node was annotated on construction
            co_return TError(expr->Location,
                "Не удалось определить тип выражения для аннотации типов: '" + std::string(expr->NodeName()) + "'.\n"
                "Возможно, это неизвестный или некорректный вид выражения.\n"
                "Проверьте корректность синтаксиса и структуры кода.\n"
                "\nПример: выражение типа ??? не поддерживается.\n"
                "Если вы считаете, что это ошибка компилятора, пожалуйста, сообщите о ней с примером кода.");
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
    auto result = DoAnnotate(expr, Context, NSemantics::TScopeId{0}).result();
    if (!result) {
        return result;
    }
    // Append monomorphized generic-function clones to the top-level block so
    // lowering compiles them exactly like ordinary top-level functions — see
    // GetGenericInstantiations' comment for why. Drained (not just read):
    // Annotate runs once per fixed-point iteration of the same top-level
    // block (NTransform::Pipeline's do/while loop), so a non-draining read
    // would re-splice already-spliced clones every time.
    if (auto block = TMaybeNode<TBlockExpr>(*result)) {
        for (auto& fdecl : Context.TakeGenericInstantiations()) {
            block.Cast()->Stmts.push_back(fdecl);
        }
    }
    return result;
}

} // namespace NTypeAnnotation
} // namespace NQumir
