#include "lexer.h"

#include <map>
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
    std::variant<uint64_t,double,std::monostate> number = std::monostate();
    int frac = 10;
    bool repeat = false;

    auto flush =[&]() {
        if (!std::holds_alternative<std::monostate>(number)) {
            TToken::UPrimitive value;
            bool isFloat = false;
            if (std::holds_alternative<uint64_t>(number)) {
                value.u64 = std::get<uint64_t>(number);
            } else {
                value.f64 = std::get<double>(number);
                isFloat = true;
            }
            Tokens.emplace_back(TToken {
                .Value = value,
                .Type = isFloat ? TToken::Float : TToken::Integer,
                .Location = CurrentLocation
            });
            number = std::monostate();
            frac = 10;
        }

        State = Start;
        repeat = true;
    };

    while ((Tokens.empty() || State != Start) && In.get(ch)) {
        if (ch == '\n') {
            Tokens.emplace_back(TToken {
                .Value = {.u64 = (uint64_t)EOperator::Eol},
                .Type = TToken::Operator,
                .Location = CurrentLocation
            });
            CurrentLocation.Line++;
            CurrentLocation.Column = 0;
            continue;
        } else {
            CurrentLocation.Column++;
        }

        do {
            repeat = false;
            switch (State) {
                case Start:
                    if (std::isdigit(ch)) {
                        State = InNumber;
                        number = (uint64_t)(ch - '0');
                    } else {
                        throw std::runtime_error("unexpected character");
                    }
                case InNumber:
                    if (std::isdigit(ch)) {
                        if (std::holds_alternative<double>(number)) {
                            number = (double)(std::get<double>(number) * frac + (ch - '0')) / frac;
                            frac *= 10;
                        } else {
                            number = (uint64_t)(std::get<uint64_t>(number) * 10 + (ch - '0'));
                        }
                    } else if (ch == '.') {
                        // TODO: support scientific notation i.e. 1e10
                        if (std::holds_alternative<double>(number)) {
                            // Second dot in a number
                            flush();
                        } else {
                            number = (double)(std::get<uint64_t>(number));
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
