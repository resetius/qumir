#include "parser.h"
#include "qumir/parser/type.h"

#include <qumir/error.h>
#include <qumir/optional.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/operator.h>

#include <iostream>

namespace NQumir {
namespace NAst {

namespace {

inline TExprPtr list(TLocation loc, std::vector<TExprPtr> elements) {
    return std::make_shared<TBlockExpr>(std::move(loc), std::move(elements));
}


using TAstTask = TExpectedTask<TExprPtr, TError, TLocation>;
TAstTask stmt(TTokenStream& stream);
TAstTask stmt_list(TTokenStream& stream);
TExpectedTask<std::vector<std::shared_ptr<TVarStmt>>, TError, TLocation> var_decl_list(TTokenStream& stream, bool parseAttributes = false, bool isMutable = true);

// Convert a keyword token to its textual form (RU) so it can be part of an identifier name
inline std::string KeywordToString(EKeyword kw) {
    switch (kw) {
        case EKeyword::False: return "ложь";
        case EKeyword::True: return "истина";
        case EKeyword::Alg: return "алг";
        case EKeyword::Begin: return "нач";
        case EKeyword::End: return "кон";
        case EKeyword::If: return "если";
        case EKeyword::Then: return "то";
        case EKeyword::Else: return "иначе";
        case EKeyword::Break: return "все";
        case EKeyword::Continue: return "далее";
        case EKeyword::Switch: return "выбор";
        case EKeyword::Case: return "при";
        case EKeyword::LoopStart: return "нц";
        case EKeyword::LoopEnd: return "кц";
        case EKeyword::LoopEndWhen: return "кц_при";
        case EKeyword::Input: return "ввод";
        case EKeyword::Output: return "вывод";
        case EKeyword::Int: return "цел";
        case EKeyword::Float: return "вещ";
        case EKeyword::Bool: return "лог";
        case EKeyword::String: return "лит";
        case EKeyword::Array: return "таб";
        case EKeyword::For: return "для";
        case EKeyword::From: return "от";
        case EKeyword::To: return "до";
        case EKeyword::Step: return "шаг";
        case EKeyword::Times: return "раз";
        case EKeyword::NewLine: return "нс";
        case EKeyword::And: return "и";
        case EKeyword::Or: return "или";
        case EKeyword::Not: return "не";
        case EKeyword::Div: return "div";
        case EKeyword::Mod: return "mod";
        case EKeyword::Sqrt: return "sqrt";
        case EKeyword::Abs: return "abs";
        case EKeyword::Iabs: return "iabs";
        case EKeyword::Sign: return "sign";
        case EKeyword::Sin: return "sin";
        case EKeyword::Cos: return "cos";
        case EKeyword::Tan: return "tg";
        case EKeyword::Ctg: return "ctg";
        case EKeyword::Ln: return "ln";
        case EKeyword::Lg: return "lg";
        case EKeyword::Min: return "min";
        case EKeyword::Max: return "max";
        case EKeyword::Exp: return "exp";
        case EKeyword::IntFunc: return "int";
        case EKeyword::Rnd: return "rnd";
    }
    return "";
}

inline bool IsTypeKeyword(EKeyword kw) {
    return kw == EKeyword::Int
        || kw == EKeyword::Float
        || kw == EKeyword::Bool
        || kw == EKeyword::String
        || kw == EKeyword::Array
        || kw == EKeyword::InArg // for function parameter declarations
        || kw == EKeyword::OutArg // for function parameter declarations
        ;
}

// Read a variable name possibly consisting of multiple tokens (identifiers and/or keywords),
// stopping before ',' or EOL, leaving the stop token in the stream.
std::expected<std::string, TError> ReadName(TTokenStream& stream) {
    std::string name;
    bool sawAny = false;
    while (true) {
        auto t = stream.Next();
        if (!t.has_value()) {
            // EOF — finish if we have a name, otherwise error
            if (sawAny) {
                return name;
            }
            return std::unexpected(TError(stream.GetLocation(), "ожидалось имя переменной"));
        }
        if (t->Type == TToken::Operator) {
            auto op = static_cast<EOperator>(t->Value.i64);
            if (op == EOperator::Comma || op == EOperator::Eol) {
                // Stop and put it back for the caller to consume
                stream.Unget(*t);
                if (sawAny) {
                    return name;
                }
                return std::unexpected(TError(t->Location, "ожидалось имя переменной перед разделителем"));
            } else {
                return std::unexpected(TError(t->Location, "недопустимый оператор внутри имени переменной"));
            }
        } else if (t->Type == TToken::Identifier) {
            if (!name.empty()) name += ' ';
            name += t->Name;
            sawAny = true;
        } else if (t->Type == TToken::Keyword) {
            if (!name.empty()) name += ' ';
            name += KeywordToString(static_cast<EKeyword>(t->Value.i64));
            sawAny = true;
        } else {
            return std::unexpected(TError(t->Location, "недопустимый токен внутри имени переменной"));
        }
    }
}

/*
StmtList ::= Stmt*
*/
TAstTask stmt_list(TTokenStream& stream) {
    std::vector<TExprPtr> stmts;
    while (true) {
        // Skip standalone EOLs between statements
        bool skipped = false;
        while (true) {
            auto t = stream.Next();
            if (!t) break;
            if (t->Type == TToken::Operator && static_cast<EOperator>(t->Value.i64) == EOperator::Eol) {
                skipped = true;
                continue;
            }
            stream.Unget(*t);
            break;
        }
        (void)skipped;

        // Stop conditions: EOF or block terminator 'кон' (End)
        if (auto t = stream.Next()) {
            if (t->Type == TToken::Keyword && static_cast<EKeyword>(t->Value.i64) == EKeyword::End) {
                // Leave 'кон' for the caller (e.g., fun_decl) to consume
                stream.Unget(*t);
                break;
            }
            stream.Unget(*t);
        } else {
            break; // EOF
        }

        auto s = co_await stmt(stream);
        // Flatten if stmt returned a Block of multiple declarations
        if (auto blk = TMaybeNode<TVarsBlockExpr>(s)) {
            auto be = blk.Cast();
            for (auto& ch : be->Vars) {
                stmts.push_back(ch);
            }
        } else {
            stmts.push_back(std::move(s));
        }
    }
    co_return list(stream.GetLocation(), std::move(stmts));
}

/*
VarDecl ::= TypeKw Name (',' Name)* EOL
TypeKw  ::= 'цел' | 'вещ' | 'лог' | 'лит' | 'таб'
Name    ::= NamePart (' ' NamePart)*
NamePart::= Identifier | Keyword
*/
TExpectedTask<std::shared_ptr<TVarStmt>, TError, TLocation> var_decl(TTokenStream& stream, TTypePtr scalarType, bool isArray, bool isPointer) {
    auto nameTok = co_await stream.Next();
    if (nameTok.Type != TToken::Identifier) {
        // Если здесь пошёл новый тип — пусть внешняя логика разрулит (мы вернём ошибку)
        co_return TError(nameTok.Location, "ожидался идентификатор переменной");
    }

    std::string name = nameTok.Name;
    TTypePtr varType = scalarType;

    if (isArray) {
        // ожидаем границы массива: '[' int ':' int ']'
        auto t = stream.Next();
        if (!t || t->Type != TToken::Operator || static_cast<EOperator>(t->Value.i64) != EOperator::LSqBr) {
            co_return TError(stream.GetLocation(), "для табличного типа ожидаются границы массива после имени: '['");
        }
        // left bound
        auto lbTok = stream.Next();
        if (!lbTok || lbTok->Type != TToken::Integer) {
            co_return TError(stream.GetLocation(), "левая граница массива должна быть целым числом");
        }
        int left = static_cast<int>(lbTok->Value.i64);
        // ':'
        auto colonTok = stream.Next();
        if (!colonTok || colonTok->Type != TToken::Operator || static_cast<EOperator>(colonTok->Value.i64) != EOperator::Colon) {
            co_return TError(stream.GetLocation(), "ожидался ':' между границами массива");
        }
        // right bound
        auto rbTok = stream.Next();
        if (!rbTok || rbTok->Type != TToken::Integer) {
            co_return TError(stream.GetLocation(), "правая граница массива должна быть целым числом");
        }
        int right = static_cast<int>(rbTok->Value.i64);
        // ']'
        auto rsbTok = stream.Next();
        if (!rsbTok || rsbTok->Type != TToken::Operator || static_cast<EOperator>(rsbTok->Value.i64) != EOperator::RSqBr) {
            co_return TError(stream.GetLocation(), "ожидалась закрывающая ']' для границ массива");
        }

        varType = std::make_shared<TArrayType>(scalarType, left, right);
    } else {
        // скобки после имени запрещены для скалярного типа
        auto t = stream.Next();
        if (t && t->Type == TToken::Operator && static_cast<EOperator>(t->Value.i64) == EOperator::LSqBr) {
            co_return TError(t->Location, "границы массива не допускаются для скалярного типа");
        }
        if (t) {
            stream.Unget(*t);
        }
    }

    if (isPointer) {
        varType = std::make_shared<TPointerType>(varType);
    }

    auto var = std::make_shared<TVarStmt>(nameTok.Location, name, varType);
    co_return var;
}

TTypePtr getScalarType(EKeyword kw) {
    switch (kw) {
        case EKeyword::Int:
            return std::make_shared<TIntegerType>();
        case EKeyword::Float:
            return std::make_shared<TFloatType>();
        case EKeyword::Bool:
            return std::make_shared<TBoolType>();
        case EKeyword::String:
            return std::make_shared<TStringType>();
        default:
            return nullptr;
    }
}

TExpectedTask<std::vector<std::shared_ptr<TVarStmt>>, TError, TLocation> var_decl_list(TTokenStream& stream, bool parseAttributes, bool isMutable) {
    auto first = co_await stream.Next();

    bool isPointer = false;
    if (parseAttributes) {
        if (first.Type == TToken::Keyword && static_cast<EKeyword>(first.Value.i64) == EKeyword::OutArg) {
            isPointer = true;
            isMutable = true; // mutability of underlying data is implied by being an out-parameter
            first = co_await stream.Next();
        }
        else if (first.Type == TToken::Keyword && static_cast<EKeyword>(first.Value.i64) == EKeyword::InArg) {
            // skip
            first = co_await stream.Next();
        }
    }

    if (! (first.Type == TToken::Keyword && IsTypeKeyword(static_cast<EKeyword>(first.Value.i64)))) {
        co_return TError(first.Location, "ожидается тип переменной");
    }

    // Parse one or more names
    std::vector<std::shared_ptr<TVarStmt>> decls;
    TTypePtr scalarType = getScalarType(static_cast<EKeyword>(first.Value.i64));
    if (!scalarType) {
        co_return TError(first.Location, "неизвестный тип переменной");
    }
    scalarType->Mutable = isMutable;
    // опциональный признак массива после базового типа: 'таб'
    bool isArray = false;
    if (auto t = stream.Next()) {
        if (t->Type == TToken::Keyword && static_cast<EKeyword>(t->Value.i64) == EKeyword::Array) {
            isArray = true;
        } else {
            stream.Unget(*t);
        }
    }
    while (true) {
        decls.push_back(co_await var_decl(stream, scalarType, isArray, isPointer));

        auto t = stream.Next();
        if (!t) {
            // EOF ends the statement
            break;
        }
        if (t->Type == TToken::Operator) {
            auto op = static_cast<EOperator>(t->Value.i64);
            if (op == EOperator::Comma) {
                // после запятой может идти либо следующее имя, либо новый базовый тип —
                // в последнем случае завершаем текущий стейтмент
                auto look = stream.Next();
                if (look && look->Type == TToken::Keyword && IsTypeKeyword(static_cast<EKeyword>(look->Value.i64))) {
                    // новый стейтмент начинается с базового типа
                    stream.Unget(*look);
                    break;
                }
                if (look) stream.Unget(*look);
                continue;
            }
            if (op == EOperator::Eol) {
                // end of declaration statement
                break;
            }
            if (op == EOperator::RParen) {
                // Likely end of parameter list in function declaration
                stream.Unget(*t);
                break;
            }
            // Unexpected operator
            co_return TError(t->Location, "ожидалась ',' или перевод строки после имени переменной");
        } else {
            // Something else after name — error for now
            co_return TError(t->Location, "недопустимый токен после имени переменной");
        }
    }

    co_return decls;
}


/*
FunDecl ::= 'алг' EOL? 'нач' EOL? StmtList 'кон'                   // main: имя пропущено; EOL после 'алг' допустим
         | 'алг' Ident OptSignature EOL? 'нач' EOL? StmtList 'кон' // именованная; EOL между 'алг' и именем не допускается
OptSignature ::= '(' ParamList? ')'
ParamList ::= Param (',' Param)*
Param ::= 'рез' TypeSpec IdentList | 'арг' TypeSpec IdentList
TypeSpec ::= TypeKw ArrayMark?
TypeKw ::= 'цел' | 'вещ' | 'лог' | 'лит'
ArrayMark ::= 'таб'  [массивные параметры, если используются]
IdentList ::= Ident (',' Ident)*
*/
TAstTask fun_decl(TTokenStream& stream) {
    auto next = co_await stream.Next();
    TTypePtr returnType = std::make_shared<TVoidType>();
    std::vector<std::shared_ptr<TVarStmt>> args;
    std::string name = "<main>";

    if (next.Type == TToken::Keyword && IsTypeKeyword(static_cast<EKeyword>(next.Value.i64))) {
        // function return type
        returnType = getScalarType(static_cast<EKeyword>(next.Value.i64));
        next = co_await stream.Next();
    }

    if (next.Type == TToken::Identifier) {
        name = next.Name;

        // parse signature
        next = co_await stream.Next();
        if (next.Type == TToken::Operator && static_cast<EOperator>(next.Value.i64) == EOperator::LParen) {
            // '(' ... ')'
            while (true) {
                next = co_await stream.Next();
                if (next.Type == TToken::Keyword && IsTypeKeyword(static_cast<EKeyword>(next.Value.i64))) {
                    stream.Unget(next);
                    auto tmpArgs = co_await var_decl_list(stream, true, false);
                    args.insert(args.end(), tmpArgs.begin(), tmpArgs.end());
                } else {
                    break;
                }
            }

            if (! (next.Type == TToken::Operator && static_cast<EOperator>(next.Value.i64) == EOperator::RParen)) {
                co_return TError(next.Location, "ожидалась закрывающая скобка ')' после списка параметров функции");
            }

            next = co_await stream.Next();
        }
        // else: no signature (empty parameter list)
    }

    if (next.Type == TToken::Operator && static_cast<EOperator>(next.Value.i64) == EOperator::Eol) {
        // optional EOL after 'алг' or after return type
        next = co_await stream.Next();
    }

    if (! (next.Type == TToken::Keyword && static_cast<EKeyword>(next.Value.i64) == EKeyword::Begin)) {
        co_return TError(next.Location, "ожидалось 'нач' после заголовка функции");
    }

    auto body = co_await stmt_list(stream);

    next = co_await stream.Next();
    if (! (next.Type == TToken::Keyword && static_cast<EKeyword>(next.Value.i64) == EKeyword::End)) {
        co_return TError(next.Location, "ожидалось 'кон' в конце функции");
    }

    if (auto maybeBlock = TMaybeNode<TBlockExpr>(body)) {
        // ok
        co_return std::make_shared<TFunDecl>(next.Location, name, std::move(args), std::move(maybeBlock.Cast()), returnType);
    } else {
        co_return TError(body->Location, "ожидался блок операторов в теле функции");
    }
}

/*
Stmt ::= VarDecl
    | Assign
    | Input
    | Output
    | If
    | Loop
    | Switch
    | Break
    | Continue
    | FunDecl
*/
TAstTask stmt(TTokenStream& stream) {
    // Variable declarations:
    //   (цел|вещ|лог|лит|таб) Name (',' Name)* EOL
    // Names may consist of identifiers and/or keywords (multi-word), e.g. "не готов", "если число"
    // If no matching statement type found yet, return error (to be extended later).

    auto first = stream.Next();

    if (!first) {
        co_return TError(stream.GetLocation(), "ожидался стейтмент, но достигнут конец файла");
    }

    if (first->Type == TToken::Keyword && IsTypeKeyword(static_cast<EKeyword>(first->Value.i64))) {
        stream.Unget(*first);
        auto decls = co_await var_decl_list(stream);
        co_return std::make_shared<TVarsBlockExpr>(first->Location, decls);
    } else if (first->Type == TToken::Keyword && static_cast<EKeyword>(first->Value.i64) == EKeyword::Alg) {
        co_return co_await fun_decl(stream);
    } else {
        stream.Unget(*first);
        co_return TError(stream.GetLocation(), "неизвестный стейтмент (пока поддерживаются только объявления переменных)");
    }
}

} // namespace

std::expected<TExprPtr, TError> TParser::parse(TTokenStream& stream)
{
    auto task = stmt_list(stream);
    return task.result();
}

} // namespace NAst
} // namespace NQumir
