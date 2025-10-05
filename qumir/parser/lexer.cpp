#include "lexer.h"
#include "qumir/location.h"

#include <map>
#include <string>
#include <vector>
#include <variant>
#include <iostream>

namespace NQumir {
namespace NAst {

namespace {

std::map<std::string, EKeyword> KeywordMapRu = {
    {"ложь", EKeyword::False},
    {"истина", EKeyword::True},
    {"алг", EKeyword::Alg},
    {"нач", EKeyword::Begin},
    {"кон", EKeyword::End},
    {"если", EKeyword::If},
    {"то", EKeyword::Then},
    {"иначе", EKeyword::Else},
    {"все", EKeyword::Break},
    {"всё", EKeyword::Break},
    {"далее", EKeyword::Continue},
    {"нц", EKeyword::LoopStart},
    {"кц", EKeyword::LoopEnd},
    {"кц_при", EKeyword::LoopEndWhen},
    {"ввод", EKeyword::Input},
    {"вывод", EKeyword::Output},
    {"цел", EKeyword::Int},
    {"вещ", EKeyword::Float},
    {"лог", EKeyword::Bool},
    {"лит", EKeyword::String},
    {"таб", EKeyword::Array},
    {"для", EKeyword::For},
    {"пока", EKeyword::While},
    {"от", EKeyword::From},
    {"до", EKeyword::To},
    {"шаг", EKeyword::Step},
    {"раз", EKeyword::Times},
    {"выбор", EKeyword::Switch},
    {"при", EKeyword::Case},
    {"нс", EKeyword::NewLine},
    {"арг", EKeyword::InArg},
    {"рез", EKeyword::OutArg},
    {"знач", EKeyword::Return},

    // math functions
    {"sqrt", EKeyword::Sqrt},
    {"abs", EKeyword::Abs},
    {"iabs", EKeyword::Iabs},
    {"sign", EKeyword::Sign},
    {"sin", EKeyword::Sin},
    {"cos", EKeyword::Cos},
    {"tg", EKeyword::Tan},
    {"ctg", EKeyword::Ctg},
    {"ln", EKeyword::Ln},
    {"lg", EKeyword::Lg},
    {"min", EKeyword::Min},
    {"max", EKeyword::Max},
    {"exp", EKeyword::Exp},
    {"int", EKeyword::IntFunc},
    {"rnd", EKeyword::Rnd},
};

std::map<std::string, EOperator> OperatorMap = {
    {":=", EOperator::Assign},
    {"**", EOperator::Pow},
    {"*", EOperator::Mul},
    {"/", EOperator::FDiv},
    {"+", EOperator::Plus},
    {"-", EOperator::Minus},
    {"=", EOperator::Eq},
    {"<>", EOperator::Neq},
    {"<", EOperator::Lt},
    {">", EOperator::Gt},
    {"<=", EOperator::Leq},
    {">=", EOperator::Geq},
    {"(", EOperator::LParen},
    {")", EOperator::RParen},
    {"[", EOperator::LSqBr},
    {"]", EOperator::RSqBr},
    {":", EOperator::Colon},
    {",", EOperator::Comma},

    {"и", EOperator::And},
    {"или", EOperator::Or},
    {"не", EOperator::Not},
    {"div", EOperator::Div},
    {"mod", EOperator::Mod},
};

enum class ELexMode {
    LValueStart,
    Expr,
    Decl
};

enum EState {
    Start,
    InNumber,
    InString,
    InIdentifier,
    InMaybeComment,
    InMaybeNumber,
    InMaybeOperator,
    InLineComment,
    InBlockComment,
    InBlockCommentEnd
};

struct TStringLiteral {
    std::string Value;

    void Append(char ch) {
        Value += ch;
    }

    void AppendUnescaped(char ch) {
        switch (ch) {
            case 'n': Value += '\n'; break;
            case 't': Value += '\t'; break;
            case '"': Value += '"'; break;
            case '\\': Value += '\\'; break;
            default:
                throw std::runtime_error("unknown escape sequence: \\" + std::string(1, ch));
        }
    }
};

struct TIdentifierList {
    std::vector<std::string> Words;

    bool Append(char ch) {
        if (std::isspace(ch)) {
            if (!Words.empty() && !Words.back().empty()) {
                Words.emplace_back();
            }
            return true;
        }
        if (Words.empty()) {
            Words.emplace_back();
        }
        if (std::isdigit(ch) && Words.back().empty()) {
            // identifier cannot start with a digit
            return false;
        }
        Words.back() += ch;
        return true;
    }
};

} // namespace

TTokenStream::TTokenStream(std::istream& in)
    : In(in)
{ }

std::optional<TToken> TTokenStream::Next() {
    if (Tokens.empty()) {
        Read();
    }
    if (Tokens.empty()) {
        return {};
    }
    TToken token = std::move(Tokens.front()); Tokens.pop_front();
    return token;
}

void TTokenStream::Read() {
    char ch = 0;
    char prev = 0; // for 2-char operators
    std::variant<int64_t,double,TIdentifierList,TStringLiteral,std::monostate> token = std::monostate();
    int frac = 10;
    int sign = 1; // for floats
    bool repeat = false;
    bool unescape = false;
    TLocation tokenLocation = CurrentLocation;
    EState state = Start;

    auto emitKeyword = [&](EKeyword kw) {
        Tokens.emplace_back(TToken {
            .Value = {.i64 = static_cast<int64_t>(kw)},
            .Type = TToken::Keyword,
            .Location = tokenLocation
        });
    };

    auto emitOperator = [&](EOperator op) {
        Tokens.emplace_back(TToken {
            .Value = {.i64 = static_cast<int64_t>(op)},
            .Type = TToken::Operator,
            .Location = tokenLocation
        });
    };

    auto emitIdentifier = [&](const std::string& name) {
        if (name.substr(0, 2) == "__") {
            throw std::runtime_error("identifiers starting with '__' are reserved");
        }
        Tokens.emplace_back(TToken {
            .Name = name,
            .Type = TToken::Identifier,
            .Location = tokenLocation
        });
    };

    auto flush =[&]() {
        if (std::holds_alternative<int64_t>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.i64 = std::get<int64_t>(token)},
                .Type = TToken::Integer,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<double>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.f64 = sign ? std::get<double>(token) : -std::get<double>(token)},
                .Type = TToken::Float,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<TIdentifierList>(token)) {
            auto idList = std::get<TIdentifierList>(token);
            std::string logIdentifier;
            for (auto& word : idList.Words) {
                if (word.empty()) {
                    continue;
                }
                auto maybeKw = KeywordMapRu.find(word);
                auto maybeOp = OperatorMap.find(word);
                if (maybeKw != KeywordMapRu.end()) {
                    if (!logIdentifier.empty()) {
                        emitIdentifier(logIdentifier);
                        logIdentifier.clear();
                    }
                    emitKeyword(maybeKw->second);
                } else if (maybeOp != OperatorMap.end()) {
                    if (!logIdentifier.empty()) {
                        emitIdentifier(logIdentifier);
                        logIdentifier.clear();
                    }
                    emitOperator(maybeOp->second);
                } else {
                    if (!logIdentifier.empty()) {
                        logIdentifier += " ";
                    }
                    logIdentifier += word;
                }
            }
            if (!logIdentifier.empty()) {
                emitIdentifier(logIdentifier);
            }
        } else if (std::holds_alternative<TStringLiteral>(token)) {
            Tokens.emplace_back(TToken {
                .Name = std::get<TStringLiteral>(token).Value,
                .Type = TToken::String,
                .Location = tokenLocation
            });
        }

        state = Start;
        repeat = true;
        sign = 1;
        unescape = false;
        tokenLocation = CurrentLocation;
        token = std::monostate();
        frac = 10;
    };

    // single char operators:
    // /, +, -, =, ), [, ], ,,
    // ( - prefix for comments
    auto isSingleCharOperator = [](char ch) {
        return ch == ')'
            || ch == '+'
            || ch == '/'
            || ch == '='
            || ch == ','
            || ch == '['
            || ch == ']';
    };

    auto isOperatorPrefix = [](char ch) {
        return ch == '*'  // *, **
            || ch == ':'  // :, :=
            || ch == '<'  // <, <=, <>
            || ch == '>'; // >, >=
    };

    auto isIdentifierStop = [&](char ch) {
        return isSingleCharOperator(ch) || isOperatorPrefix(ch) || ch == '(' || ch == '-' || ch == '"' || ch == '\n' || ch == ';';
    };

    while ((Tokens.empty() || state != Start) && In.get(ch)) {
        if (ch == '\n') {
            CurrentLocation.Line++;
            CurrentLocation.Column = 0;
        } else {
            CurrentLocation.Column++;
        }

        do {
            repeat = false;
            // TODO: we don't support 'не' in the middle of an identifier so far
            // e.g. in 'true' kumir 'оно не истина' could be tokenized as 'не' 'оно истина'
            // if 'оно истина' is a variable name
            switch (state) {
                case Start:
                    if (ch == '\n' || ch == ';') {
                        emitOperator(EOperator::Eol);
                        tokenLocation = CurrentLocation;
                    } else if (std::isdigit(ch)) {
                        state = InNumber;
                        token = (int64_t)(ch - '0');
                    } else if (ch == '-') {
                        state = InMaybeNumber;
                    } else if (ch == '(') {
                        state = InMaybeComment;
                    } else if (ch == '"') {
                        state = InString;
                    } else if (isOperatorPrefix(ch)) {
                        prev = ch;
                        state = InMaybeOperator; // reuse this state for 2-char operators
                    } else if (isSingleCharOperator(ch)) {
                        emitOperator(OperatorMap.at(std::string(1, ch)));
                        tokenLocation = CurrentLocation;
                    } else {
                        // identifiers/keywords: start with a letter, continue with letters, digits, underscores
                        TIdentifierList idList;
                        idList.Append(ch);
                        state = InIdentifier;
                        token = std::move(idList);
                    }
                    break;
                case InIdentifier: {
                    if (!isIdentifierStop(ch)) {
                        TIdentifierList& lst = std::get<TIdentifierList>(token);
                        if (!lst.Append(ch)) {
                            // invalid character in identifier
                            flush();
                        }
                    } else {
                        flush();
                    }
                    break;
                }
                case InString: {
                    if (ch == '"') {
                        flush();
                        repeat = false; // need to skip '"'
                    } else if (ch == '\\') {
                        unescape = true;
                    } else {
                        if (!std::holds_alternative<TStringLiteral>(token)) {
                            token = TStringLiteral{};
                        }
                        if (unescape) {
                            std::get<TStringLiteral>(token).AppendUnescaped(ch);
                            unescape = false;
                        } else {
                            std::get<TStringLiteral>(token).Append(ch);
                        }
                    }
                    break;
                }
                case InMaybeOperator: {
                    std::string opStr;
                    opStr += prev;
                    opStr += ch;
                    auto it = OperatorMap.find(opStr);
                    if (it != OperatorMap.end()) {
                        emitOperator(it->second);
                        tokenLocation = CurrentLocation;
                        state = Start;
                    } else {
                        // not a 2-char operator, just the first char
                        emitOperator(OperatorMap.at(std::string(1, prev)));
                        tokenLocation = CurrentLocation;
                        state = Start;
                        repeat = true;
                    }
                    break;
                }
                case InMaybeNumber:
                    if (ch == '-') {
                        // multiline comment: -- ...
                        state = InLineComment;
                    } else if (std::isdigit(ch)) {
                        state = InNumber;
                        token = (int64_t)(-(ch - '0'));
                    } else if (ch == '.') {
                        state = InNumber;
                        token = (double)(0.0);
                        sign = 0;
                    } else {
                        // Not a negative number, just a '-' operator followed by something else
                        emitOperator(EOperator::Minus);
                        tokenLocation = CurrentLocation;
                        state = Start;
                        repeat = true;
                    }
                    break;
                case InLineComment:
                    if (ch == '\n') {
                        state = Start;
                    }
                    break;
                case InMaybeComment:
                    // Comments: (* ... *) for block comments
                    if (ch == '*') {
                        state = InBlockComment;
                    } else {
                        // Not a comment, just a '(' operator followed by something else
                        emitOperator(EOperator::LParen);
                        tokenLocation = CurrentLocation;
                        state = Start;
                        repeat = true;
                    }
                    break;
                case InBlockComment:
                    if (ch == '*') {
                        state = InBlockCommentEnd;
                    }
                    break;
                case InBlockCommentEnd:
                    if (ch == ')') {
                        state = Start;
                    }
                    break;
                case InNumber:
                    if (std::isdigit(ch)) {
                        if (std::holds_alternative<double>(token)) {
                            token = (double)(std::get<double>(token) * frac + (ch - '0')) / frac;
                            frac *= 10;
                        } else {
                            token = (int64_t)(std::get<int64_t>(token) * 10 + (ch - '0'));
                        }
                    } else if (ch == '.') {
                        // TODO: support scientific notation i.e. 1e10
                        if (std::holds_alternative<double>(token)) {
                            // Second dot in a number
                            flush();
                        } else {
                            token = (double)(std::get<int64_t>(token));
                        }
                    } else {
                        flush();
                    }
                    break;
                default:
                    throw std::runtime_error("invalid lexer state: " + std::to_string(state) + " at " + CurrentLocation.ToString() + " after '" + std::string(1, ch) + "'");
            };
        } while (repeat);
    }

    flush();
}

void TTokenStream::Unget(TToken token) {
    Tokens.emplace_front(std::move(token));
}

const TLocation& TTokenStream::operator()() const {
    return CurrentLocation;
}

const TLocation& TTokenStream::GetLocation() const {
    return CurrentLocation;
}

} // namespace NAst
} // namespace NQumir
