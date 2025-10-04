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
    return kw == EKeyword::Int || kw == EKeyword::Float || kw == EKeyword::Bool || kw == EKeyword::String || kw == EKeyword::Array;
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

        // EOF?
        if (auto t = stream.Next()) {
            stream.Unget(*t);
        } else {
            break; // EOF
        }

        auto s = co_await stmt(stream);
        // Flatten if stmt returned a Block of multiple declarations
        if (auto blk = TMaybeNode<TBlockExpr>(s)) {
            auto be = blk.Cast();
            for (auto& ch : be->Stmts) {
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
TAstTask var_decl(TTokenStream& stream, TTypePtr scalarType, bool isArray) {
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

    auto var = std::make_shared<TVarStmt>(nameTok.Location, name, varType);
    co_return var;
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
        // Parse one or more names
        std::vector<TExprPtr> decls;
        TTypePtr scalarType;
        switch (static_cast<EKeyword>(first->Value.i64)) {
            case EKeyword::Int:
                scalarType = std::make_shared<TIntegerType>();
                break;
            case EKeyword::Float:
                scalarType = std::make_shared<TFloatType>();
                break;
            case EKeyword::Bool:
                scalarType = std::make_shared<TBoolType>();
                break;
            case EKeyword::String:
                scalarType = std::make_shared<TStringType>();
                break;
            default:
                co_return TError(first->Location, "неизвестный тип переменной");
        }
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
            decls.push_back(co_await var_decl(stream, scalarType, isArray));

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
                // Unexpected operator
                co_return TError(t->Location, "ожидалась ',' или перевод строки после имени переменной");
            } else {
                // Something else after name — error for now
                co_return TError(t->Location, "недопустимый токен после имени переменной");
            }
        }

        co_return std::make_shared<TBlockExpr>(first->Location, std::move(decls));
    } else {
        // Not a variable declaration; put token back for future handlers when added
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
