#include "parser.h"
#include "qumir/parser/type.h"

#include <qumir/error.h>
#include <qumir/optional.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/operator.h>

#include <set>
#include <iostream>

namespace NQumir {
namespace NAst {

namespace {

inline TExprPtr list(TLocation loc, std::vector<TExprPtr> elements) {
    return std::make_shared<TBlockExpr>(std::move(loc), std::move(elements));
}

inline TExprPtr unary(TLocation loc, TOperator op, TExprPtr operand) {
    return std::make_shared<TUnaryExpr>(std::move(loc), op, std::move(operand));
}

inline TExprPtr binary(TLocation loc, TOperator op, TExprPtr left, TExprPtr right) {
    return std::make_shared<TBinaryExpr>(std::move(loc), op, std::move(left), std::move(right));
}

inline TExprPtr num(TLocation loc, int64_t v) {
    return std::make_shared<TNumberExpr>(std::move(loc), v);
}

inline TExprPtr num(TLocation loc, double v) {
    return std::make_shared<TNumberExpr>(std::move(loc), v);
}

inline TExprPtr num(TLocation loc, bool v) {
    return std::make_shared<TNumberExpr>(std::move(loc), v);
}

inline TExprPtr ident(TLocation loc, std::string n) {
    return std::make_shared<TIdentExpr>(loc, std::move(n));
}

using TAstTask = TExpectedTask<TExprPtr, TError, TLocation>;
TAstTask stmt(TTokenStream& stream);
TAstTask stmt_list(TTokenStream& stream, std::set<EKeyword> terminators);
TExpectedTask<std::vector<std::shared_ptr<TVarStmt>>, TError, TLocation> var_decl_list(TTokenStream& stream, bool parseAttributes = false, bool isMutable = true);
TAstTask expr(TTokenStream& stream);

void SkipEols(TTokenStream& stream) {
    while (true) {
        auto t = stream.Next();
        if (!t) break;
        if (t->Type == TToken::Operator && static_cast<EOperator>(t->Value.i64) == EOperator::Eol) {
            continue;
        }
        stream.Unget(*t);
        break;
    }
}

/*
enum class EOperator : uint8_t {
    // Arithmetic operators
    Pow, // **
    Mul, // *
    FDiv, // /
    Plus, // +
    Minus, // -
    // Comparison operators
    Eq, // =
    Neq, // <>
    Lt, // <
    Gt, // >
    Leq, // <=
    Geq, // >=
    // Other operators
    Assign, // :=
    Comma, // ,
    LParen, // (
    RParen, // )
    LSqBr, // [
    RSqBr, // ]
    Colon, // :
    // Special operators
    Eol, // \n
    // Logical operators
    And,
    Or,
    Not,
    // Integer division and modulus
    Div,
    Mod,
};
*/
inline TOperator MakeOperator(EOperator op) {
    switch (op) {
        case EOperator::Pow: return '^';
        case EOperator::Mul: return '*';
        case EOperator::FDiv: return '/';
        case EOperator::Plus: return '+';
        case EOperator::Minus: return '-';

        case EOperator::Eq: return TOperator("==");
        case EOperator::Neq: return TOperator("!=");
        case EOperator::Lt: return TOperator("<");
        case EOperator::Gt: return TOperator(">");
        case EOperator::Leq: return TOperator("<=");
        case EOperator::Geq: return TOperator(">=");

        case EOperator::And: return TOperator("&&");
        case EOperator::Or: return TOperator("||");
        case EOperator::Not: return TOperator("!");

        case EOperator::Div: return TOperator("//");
        case EOperator::Mod: return TOperator("%");

        default:
            throw std::runtime_error("internal error: unknown operator");
    }
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

/*
StmtList ::= Stmt*
*/
TAstTask stmt_list(TTokenStream& stream, std::set<EKeyword> terminators) {
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
            if (t->Type == TToken::Keyword && terminators.contains(static_cast<EKeyword>(t->Value.i64))) {
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

    auto body = co_await stmt_list(stream, { EKeyword::End });

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
If ::= 'если' Expr EOL* 'то' EOL* StmtList OptElse 'все'
OptElse ::= EOL* 'иначе' EOL* StmtList | ε
// Примечания:
// - Expr, StmtList не раскрываются здесь (используются как чёрные ящики).
// - EOL* означает, что между элементами могут быть пустые строки/переводы строк.
// - Примеры допускают как серию на той же строке после 'то'/'иначе', так и на следующих строках.
*/
TAstTask if_expr(TTokenStream& stream) {
    auto location = stream.GetLocation();
    auto cond = co_await expr(stream);
    SkipEols(stream);

    auto thenTok = co_await stream.Next();
    if (!(thenTok.Type == TToken::Keyword && static_cast<EKeyword>(thenTok.Value.i64) == EKeyword::Then)) {
        co_return TError(thenTok.Location, "ожидалось 'то' после условия в операторе 'если'");
    }

    auto thenBranch = co_await stmt_list(stream, { EKeyword::Else, EKeyword::Break });

    SkipEols(stream);
    auto elseTok = co_await stream.Next();
    if (elseTok.Type == TToken::Keyword && static_cast<EKeyword>(elseTok.Value.i64) == EKeyword::Break) {
        // if without else
        co_return std::make_shared<TIfExpr>(location, cond, thenBranch, nullptr);
    }

    if (elseTok.Type == TToken::Keyword && static_cast<EKeyword>(elseTok.Value.i64) != EKeyword::Else) {
        co_return TError(elseTok.Location, "ожидалось 'иначе' или 'все' после ветки 'то' в операторе 'если'");
    }

    auto elseBranch = co_await stmt_list(stream, { EKeyword::Break });

    auto endTok = co_await stream.Next();
    if (endTok.Type == TToken::Keyword && static_cast<EKeyword>(endTok.Value.i64) != EKeyword::Break) {
        co_return TError(endTok.Location, "ожидалось 'все' в конце оператора 'если'");
    }

    co_return std::make_shared<TIfExpr>(location, cond, thenBranch, elseBranch);
}

// Parse optional argument list after '(' then ')'
TExpectedTask<std::vector<TExprPtr>, TError, TLocation> parse_arg_list_opt(TTokenStream& stream) {
    std::vector<TExprPtr> args;
    auto tok = co_await stream.Next();
    if (tok.Type == TToken::Operator && (EOperator)tok.Value.i64 == EOperator::RParen) {
        co_return args; // empty
    }
    stream.Unget(tok);
    // first expr
    auto e = co_await expr(stream);
    args.push_back(std::move(e));
    while (true) {
        auto t = co_await stream.Next();
        if (t.Type == TToken::Operator && (EOperator)t.Value.i64 == EOperator::RParen) {
            break;
        }
        if (t.Type == TToken::Operator && (EOperator)t.Value.i64 == EOperator::Comma) {
            auto e2 = co_await expr(stream);
            args.push_back(std::move(e2));
            continue;
        }
        stream.Unget(t);
        co_return TError(stream.GetLocation(), "ожидается ',' или ')' в списке аргументов функции");
    }
    co_return args;
}

/*
Factor/Primary ::= Number | Ident | ( Expr ) | fun
*/
TAstTask factor(TTokenStream& stream) {
    auto token = co_await stream.Next();
    if (token.Type == TToken::Integer) {
        co_return num(token.Location, token.Value.i64);
    } else if (token.Type == TToken::Float) {
        co_return num(token.Location, token.Value.f64);
    } else if (token.Type == TToken::Identifier) {
        co_return ident(token.Location, token.Name);
    } else if (token.Type == TToken::Operator) {
        if ((EOperator)token.Value.i64 == EOperator::LParen) {
            auto ret = co_await expr(stream);
            token = co_await stream.Next();
            if (token.Type != TToken::Operator || (EOperator)token.Value.i64 != EOperator::RParen) {
                co_return TError(stream.GetLocation(), std::string("ожидается ')'"));
            }
            co_return ret;
        } else {
            co_return TError(stream.GetLocation(), std::string("неожиданный оператор"));
        }
    } else if (token.Type == TToken::Keyword && ((EKeyword)token.Value.i64 == EKeyword::True || (EKeyword)token.Value.i64 == EKeyword::False)) {
        bool v = (EKeyword)token.Value.i64 == EKeyword::True;
        co_return num(token.Location, v);
    } else {
        co_return TError(stream.GetLocation(), std::string("ожидалось число или '('"));
    }
}

// call_expr ::= factor ( '(' arg_list_opt ')' )*
TAstTask call_expr(TTokenStream& stream) {
    auto base = co_await factor(stream);
    while (auto tok = stream.Next()) {
        if (tok->Type == TToken::Operator && (EOperator)tok->Value.i64 == EOperator::LParen) {
            // Разрешаем вызов функции только если базовое выражение — идентификатор
            if (!TMaybeNode<TIdentExpr>(base)) {
                co_return TError(tok->Location, "ожидалось имя функции перед '('");
            }
            auto args = co_await parse_arg_list_opt(stream);
            base = std::make_shared<TCallExpr>(tok->Location, std::move(base), std::move(args));
            continue;
        }
        stream.Unget(*tok);
        break;
    }
    co_return base;
}

// Forward declaration for mutual recursion with power_expr
TAstTask unary_expr(TTokenStream& stream);

// power_expr ::= call_expr ( '**' unary_expr )?
// Right-associative: a ** b ** c == a ** (b ** c)
TAstTask power_expr(TTokenStream& stream) {
    auto base = co_await call_expr(stream);
    auto tok = stream.Next();
    if (tok && tok->Type == TToken::Operator && (EOperator)tok->Value.i64 == EOperator::Pow) {
        // RHS allows unary sign, e.g., 2 ** -3
        auto rhs = co_await unary_expr(stream);
        co_return binary(tok->Location, MakeOperator(EOperator::Pow), std::move(base), std::move(rhs));
    }
    if (tok) stream.Unget(*tok);
    co_return base;
}

// unary ::= call_expr | '+' unary | '-' unary
TAstTask unary_expr(TTokenStream& stream) {
    auto tok = co_await stream.Next();
    if (tok.Type == TToken::Operator && ((EOperator)tok.Value.i64 == EOperator::Plus || (EOperator)tok.Value.i64 == EOperator::Minus)) {
        auto inner = co_await unary_expr(stream);
        if ((EOperator)tok.Value.i64 == EOperator::Plus) {
            co_return unary(tok.Location, MakeOperator(EOperator::Plus), std::move(inner));
        } else {
            co_return unary(tok.Location, MakeOperator(EOperator::Minus), std::move(inner));
        }
    }
    stream.Unget(tok);
    // Exponentiation has higher precedence than unary: -2**2 == -(2**2)
    co_return co_await power_expr(stream);
}

template<typename Func, typename... TOps>
TAstTask binary_op_helper(TTokenStream& stream, Func prev, TOps... ops) {
    auto ret = co_await prev(stream);
    while (auto token = stream.Next()) {
        if (token->Type == TToken::Operator
            && ((token->Value.i64 == (int64_t)ops) || ...))
        {
            auto next = co_await prev(stream);
            ret = binary(token->Location, MakeOperator((EOperator)token->Value.i64), std::move(ret), std::move(next));
        } else {
            stream.Unget(*token);
            break;
        }
    }
    co_return ret;
}

/*
MulExpr ::= Factor
         | MulExpr*Factor
         | MulExpr/Factor
*/
TAstTask mul_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, unary_expr
        , EOperator::Mul, EOperator::Div, EOperator::FDiv, EOperator::Mod);
}

/*
AddExpr ::= MulExpr
         | AddExpr+MulExpr
         | AddExpr-MulExpr
*/
TAstTask add_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, mul_expr, EOperator::Plus, EOperator::Minus);
}

/* RelExpr ::= AddExpr (("<" | "<=" | ">" | ">=") AddExpr)*  */
TAstTask rel_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, add_expr, EOperator::Lt, EOperator::Gt, EOperator::Leq, EOperator::Geq);
}

/* EqExpr ::= RelExpr (("==" | "!=") RelExpr)* */
TAstTask eq_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, rel_expr, EOperator::Eq, EOperator::Neq);
}

/* AndExpr ::= EqExpr ( "&&" EqExpr )* */
TAstTask and_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, eq_expr, EOperator::And);
}

/* OrExpr ::= AndExpr ( "||" OrExpr )* */
TAstTask or_expr(TTokenStream& stream) {
    co_return co_await binary_op_helper(stream, and_expr, EOperator::Or);
}

TAstTask expr(TTokenStream& stream) {
    co_return co_await or_expr(stream);
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
    } else if (first->Type == TToken::Keyword && static_cast<EKeyword>(first->Value.i64) == EKeyword::If) {
        co_return co_await if_expr(stream);
    } else if (first->Type == TToken::Keyword && static_cast<EKeyword>(first->Value.i64) == EKeyword::Return) {
        // skip ':='
        auto next = co_await stream.Next();
        if (!(next.Type == TToken::Operator && static_cast<EOperator>(next.Value.i64) == EOperator::Assign)) {
            stream.Unget(next);
            co_return TError(stream.GetLocation(), "ожидался ':=' после 'знач'");
        }
        auto value = co_await expr(stream);
        co_return std::make_shared<TReturnExpr>(first->Location, value);
    } else if (first->Type == TToken::Identifier) {
        auto next = co_await stream.Next();
        if (next.Type == TToken::Operator && static_cast<EOperator>(next.Value.i64) == EOperator::Assign) {
            // Assignment statement
            auto rhs = co_await expr(stream);
            co_return std::make_shared<TAssignExpr>(first->Location, first->Name, rhs);
        } else {
            stream.Unget(*first);
            stream.Unget(next);
            co_return TError(stream.GetLocation(), "неизвестный стейтмент (пока поддерживаются только объявления переменных)");
        }
    } else {
        // std::cerr << "Debug " << (int)first->Type << " " << first->Name << " " << first->Value.i64 << "\n";
        stream.Unget(*first);
        co_return TError(stream.GetLocation(), "неизвестный стейтмент (пока поддерживаются только объявления переменных)");
    }
}

} // namespace

std::expected<TExprPtr, TError> TParser::parse(TTokenStream& stream)
{
    auto task = stmt_list(stream, {});
    return task.result();
}

} // namespace NAst
} // namespace NQumir
