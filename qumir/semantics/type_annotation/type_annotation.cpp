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
                co_return TError(unary->Location, "Нельзя применять унарный минус к беззнаковому целому типу");
            }
            co_return unary;
        }
        auto maybeFloat = TMaybeType<TFloatType>(unary->Type);
        if (maybeFloat) {
            co_return unary;
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

    co_await DoAnnotate(funDecl->Body, context, scopeId);
    if (!funDecl->Body->Type) {
        co_return TError(funDecl->Location, "Не удалось определить тип тела функции");
    }

    co_return funDecl;
}

TTask AnnotateBinary(std::shared_ptr<TBinaryExpr> binary, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
    binary->Left = co_await DoAnnotate(binary->Left, context, scopeId);
    binary->Right = co_await DoAnnotate(binary->Right, context, scopeId);
    if (!binary->Left->Type || !binary->Right->Type) {
        co_return TError(binary->Location, "Не удалось определить типы выражения для бинарной операции");
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
                co_return TError(binary->Location, "+, -, *, / применимы только к числам (оператор + также работает для строк)");
            }
            if (binary->Operator == TOperator("/")) {
                // 3/2 -> 1.5 : convert to float division
                common = std::make_shared<TFloatType>();
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
                    co_return TError(binary->Location, "Операции сравнения применимы только к числам");
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
            co_return TError(binary->Location, "Неизвестный бинарный оператор: '" + binary->Operator.ToString() + "'");
            break;
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
    block->Type = std::make_shared<TVoidType>();
    co_return block;
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
        if (!CanImplicit(valueType, symbolType)) {
            co_return TError(assign->Location, "Нельзя неявно преобразовать тип '" + std::string(valueType->TypeName()) + "' к типу '" + std::string(symbolType->TypeName()) + "' при присваивании переменной '" + assign->Name + "'.");
        }
        assign->Value = InsertImplicitCastIfNeeded(assign->Value, sym->Type);
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
    // Allow array assignment for arrays and strings
    auto maybeArrayType = TMaybeType<TArrayType>(sym->Type);
    auto maybeStringType = TMaybeType<TStringType>(sym->Type);
    if (maybeArrayType) {
        auto arrayType = maybeArrayType.Cast();
        auto valueType = UnwrapReferenceType(arrayAssign->Value->Type);
        if (!EqualTypes(valueType, arrayType->ElementType)) {
            if (!CanImplicit(valueType, arrayType->ElementType)) {
                co_return TError(arrayAssign->Location, "Нельзя неявно преобразовать тип '" +
                    std::string(valueType->TypeName()) + "' к типу '" + std::string(arrayType->ElementType->TypeName()) + "' при присваивании элементу массива '" + arrayAssign->Name + "'.");
            }
            arrayAssign->Value = InsertImplicitCastIfNeeded(arrayAssign->Value, arrayType->ElementType);
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
        co_return TError(arrayAssign->Location, "Идентификатор '" + arrayAssign->Name + "' не является массивом или строкой, поэтому нельзя использовать присваивание по индексу.");
    }
}

TTask AnnotateVar(std::shared_ptr<TVarStmt> var) {
    if (!var->Type) {
        co_return TError(var->Location, "Не указан тип переменной при объявлении: " + var->Name);
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

TTask AnnotateCall(std::shared_ptr<TCallExpr> call, NSemantics::TNameResolver& context, NSemantics::TScopeId scopeId) {
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
                if (!CanImplicit(UnwrapReferenceType(arg->Type), paramT)) {
                    co_return TError(arg->Location, "Аргумент #" + std::to_string(i+1) + ": нельзя неявно преобразовать тип '" + std::string(arg->Type->TypeName()) + "' к типу параметра '" + std::string(paramT->TypeName()) + "'.");
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
        co_return TError(ifExpr->Cond->Location, "Условие в `если' не имеет типа. Проверьте корректность выражения в условии.");
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
        co_return TError(ifExpr->Then->Location, "Ветвь `то' в `если' не имеет типа. Проверьте корректность выражения.");
    }
    if (ifExpr->Else) {
        ifExpr->Else = co_await DoAnnotate(ifExpr->Else, context, scopeId);
        if (!ifExpr->Else->Type) {
            co_return TError(ifExpr->Else->Location, "Ветвь `иначе' в `если' не имеет типа. Проверьте корректность выражения.");
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
            if (!CanImplicit(index->Type, intType)) {
                co_return TError(index->Location, "Индекс #" + std::to_string(i+1) + " в многомерном обращении к массиву должен быть целым числом.");
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
        co_return TError(indexExpr->Location, "В выражении индексации не удалось определить тип коллекции (массива или строки). Проверьте корректность выражения.");
    }
    indexExpr->Index = co_await DoAnnotate(indexExpr->Index, context, scopeId);
    if (!indexExpr->Index->Type) {
        co_return TError(indexExpr->Location, "Индекс в выражении индексации не имеет типа. Убедитесь, что выражение индекса корректно и его тип определён.");
    }
    auto intType = std::make_shared<TIntegerType>();
    if (!EqualTypes(indexExpr->Index->Type, intType)) {
        if (!CanImplicit(indexExpr->Index->Type, intType)) {
            co_return TError(indexExpr->Location, "Индекс в выражении индексации должен быть целым числом. Например: a[2], s[1].");
        }
        indexExpr->Index = InsertImplicitCastIfNeeded(indexExpr->Index, intType);
    }
    auto collectionType = UnwrapReferenceType(indexExpr->Collection->Type);
    if (TMaybeType<TStringType>(collectionType)) {
        indexExpr->Type = std::make_shared<TSymbolType>();
    } else if (auto maybeArrayType = TMaybeType<TArrayType>(collectionType)) {
        auto arrayType = maybeArrayType.Cast();
        indexExpr->Type = arrayType->ElementType;
    } else {
        co_return TError(indexExpr->Location, "Индексация поддерживается только для массивов и строк.\n"
            "Пример корректной индексации массива: a[2] (где a — массив).\n"
            "Пример корректной индексации строки: s[1] (где s — строка).\n"
            "Проверьте, что вы обращаетесь к массиву или строке, а не к другому типу.");
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
        if (!CanImplicit(sliceExpr->Start->Type, intType)) {
            co_return TError(sliceExpr->Location, "Начальный индекс в срезе должен быть целым числом. Пример: s[1:3].");
        }
        sliceExpr->Start = InsertImplicitCastIfNeeded(sliceExpr->Start, intType);
    }
    if (!EqualTypes(sliceExpr->End->Type, intType)) {
        if (!CanImplicit(sliceExpr->End->Type, intType)) {
            co_return TError(sliceExpr->Location, "Конечный индекс в срезе должен быть целым числом. Пример: s[1:3].");
        }
        sliceExpr->End = InsertImplicitCastIfNeeded(sliceExpr->End, intType);
    }
    // Срез строки возвращает строку
    sliceExpr->Type = collectionType;

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
    return DoAnnotate(expr, Context, NSemantics::TScopeId{0}).result();
}

} // namespace NTypeAnnotation
} // namespace NQumir
