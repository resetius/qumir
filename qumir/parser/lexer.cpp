#include "lexer.h"
#include "qumir/location.h"

#include <map>
#include <string>
#include <variant>

namespace NQumir {
namespace NAst {

namespace {

std::map<std::string, EKeyword> KeywordMapRu = {
    {"алг", EKeyword::Alg},
    {"нач", EKeyword::Begin},
    {"кон", EKeyword::End},
    {"если", EKeyword::If},
    {"то", EKeyword::Then},
    {"иначе", EKeyword::Else},
    {"все", EKeyword::All},
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
    {"от", EKeyword::From},
    {"до", EKeyword::To},
    {"шаг", EKeyword::Step},
    {"раз", EKeyword::Times},
    {"и", EKeyword::And},
    {"или", EKeyword::Or},
    {"не", EKeyword::Not},
    {"выбор", EKeyword::Switch},
    {"при", EKeyword::Case},

    // math functions
    {"div", EKeyword::Div},
    {"mod", EKeyword::Mod},
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
    std::variant<int64_t,double,std::string,std::monostate> token = std::monostate();
    int frac = 10;
    bool repeat = false;
    TLocation tokenLocation = CurrentLocation;

    auto flush =[&]() {
        if (std::holds_alternative<int64_t>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.i64 = std::get<int64_t>(token)},
                .Type = TToken::Integer,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<double>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.f64 = std::get<double>(token)},
                .Type = TToken::Float,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<std::string>(token)) {
            auto& str = std::get<std::string>(token);
            auto maybeKeyword = KeywordMapRu.find(str);
            if (maybeKeyword != KeywordMapRu.end()) {
                Tokens.emplace_back(TToken {
                    .Value = {.i64 = (int64_t)maybeKeyword->second},
                    .Type = TToken::Keyword,
                    .Location = tokenLocation
                });
            } else {
                Tokens.emplace_back(TToken {
                    .Name = str,
                    .Type = TToken::Identifier,
                    .Location = tokenLocation
                });
            }
        }

        State = Start;
        repeat = true;
        tokenLocation = CurrentLocation;
        token = std::monostate();
        frac = 10;
    };

    // single char operators:
    // /, +, -, =, ), [, ], :, ,
    // ( - prefix for comments
    auto isSingleCharOperator = [](char ch) {
        return ch == ')'
            || ch == '+'
            || ch == '/'
            || ch == '='
            || ch == ','
            || ch == ':'
            || ch == '['
            || ch == ']';
    };

    auto isOperatorPrefix = [](char ch) {
        return ch == '*'  // *, **
            || ch == '<'  // <, <=, <>
            || ch == '>'; // >, >=
    };

    while ((Tokens.empty() || State != Start) && In.get(ch)) {
        if (ch == '\n') {
            CurrentLocation.Line++;
            CurrentLocation.Column = 0;
            continue;
        } else {
            CurrentLocation.Column++;
        }

        do {
            repeat = false;
            // TODO: we don't support 'не' in the middle of an identifier so far
            // e.g. in 'true' kumir 'оно не истина' could be tokenized as 'не' 'оно истина'
            // if 'оно истина' is a variable name
            switch (State) {
                case Start:
                    if (ch == '\n') {
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)EOperator::Eol},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                    } else if (std::isdigit(ch)) {
                        State = InNumber;
                        token = (int64_t)(ch - '0');
                    } else if (ch == '-') {
                        State = InMaybeNumber;
                    } else if (ch == '(') {
                        State = InMaybeComment;
                    } else if (isOperatorPrefix(ch)) {
                        prev = ch;
                        State = InMaybeOperator; // reuse this state for 2-char operators
                    } else if (isSingleCharOperator(ch)) {
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)OperatorMap.at(std::string(1, ch))},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                    } else {
                        throw std::runtime_error("unexpected character");
                    }
                    break;
                case InMaybeOperator: {
                    std::string opStr;
                    opStr += prev;
                    opStr += ch;
                    auto it = OperatorMap.find(opStr);
                    if (it != OperatorMap.end()) {
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)it->second},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                        State = Start;
                    } else {
                        // not a 2-char operator, just the first char
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)OperatorMap.at(std::string(1, prev))},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                        State = Start;
                        repeat = true;
                    }
                    break;
                }
                case InMaybeNumber:
                    if (std::isdigit(ch)) {
                        State = InNumber;
                        token = (int64_t)(-(ch - '0'));
                    } else if (ch == '.') {
                        State = InNumber;
                        token = (double)(-0.0);
                    } else {
                        // Not a negative number, just a '-' operator followed by something else
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)OperatorMap.at(std::string(1, '-'))},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                        State = Start;
                        repeat = true;
                    }
                    break;
                case InMaybeComment:
                    // Comments: (* ... *) for block comments
                    if (ch == '*') {
                        State = InBlockComment;
                    } else {
                        // Not a comment, just a '(' operator followed by something else
                        Tokens.emplace_back(TToken {
                            .Value = {.i64 = (int64_t)OperatorMap.at(std::string(1, '('))},
                            .Type = TToken::Operator,
                            .Location = tokenLocation
                        });
                        tokenLocation = CurrentLocation;
                        State = Start;
                        repeat = true;
                    }
                    break;
                case InBlockComment:
                    if (ch == '*') {
                        State = InBlockCommentEnd;
                    }
                    break;
                case InBlockCommentEnd:
                    if (ch == ')') {
                        State = Start;
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
                default:
                    throw std::runtime_error("invalid lexer state");
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
