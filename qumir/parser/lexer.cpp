#include "lexer.h"

#include <qumir/error.h>
#include <qumir/location.h>

#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <iostream>
#include <cmath>

namespace NQumir {
namespace NAst {

namespace {

std::map<std::string, EKeyword> KeywordMapRu = {
    {"нет", EKeyword::False},
    {"да", EKeyword::True},
    {"алг", EKeyword::Alg},
    {"нач", EKeyword::Begin},
    {"кон", EKeyword::End},
    {"если", EKeyword::If},
    {"то", EKeyword::Then},
    {"иначе", EKeyword::Else},
    {"все", EKeyword::EndIf},
    {"всё", EKeyword::EndIf},
    {"выход", EKeyword::Break},
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
    {"сим", EKeyword::Char},

    {"целтаб", EKeyword::IntTab},
    {"вещтаб", EKeyword::FloatTab},
    {"логтаб", EKeyword::BoolTab},
    {"литтаб", EKeyword::StringTab},
    {"симтаб", EKeyword::CharTab},

    {"таб", EKeyword::Array},
    {"файл", EKeyword::File},
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
    {"аргрез", EKeyword::InOutArg},
    {"знач", EKeyword::Return},
    {"использовать", EKeyword::Use},
    {"утв", EKeyword::Assert},
    {"дано", EKeyword::AssertBefore},
    {"надо", EKeyword::AssertAfter}
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
};

std::string_view TrimLeft(std::string_view value) {
    while (!value.empty() && std::isspace(value.front())) {
        value.remove_prefix(1);
    }
    return value;
}

std::string_view Trim(std::string_view value) {
    value = TrimLeft(value);
    while (!value.empty() && std::isspace(value.back())) {
        value.remove_suffix(1);
    }
    return value;
}

std::string ReadPragmaWord(std::string_view& value) {
    value = TrimLeft(value);
    size_t end = 0;
    while (end < value.size() && !std::isspace(value[end]) && value[end] != ':') {
        ++end;
    }
    std::string word(value.substr(0, end));
    value.remove_prefix(end);
    return word;
}

std::optional<TPragma> ParsePragmaComment(std::string_view comment, TLocation location) {
    comment = Trim(comment);
    if (!comment.starts_with("#")) {
        return std::nullopt;
    }

    comment.remove_prefix(1);
    comment = TrimLeft(comment);
    auto namespaceName = ReadPragmaWord(comment);
    if (namespaceName != "qumir") {
        return std::nullopt;
    }

    auto group = ReadPragmaWord(comment);
    if (group.empty()) {
        throw TError(location, "ожидалась группа прагмы после 'qumir'");
    }

    comment = TrimLeft(comment);
    if (comment.empty() || comment.front() != ':') {
        throw TError(location, "ожидалось ':' после группы прагмы");
    }
    comment.remove_prefix(1);
    comment = Trim(comment);
    if (comment.empty()) {
        throw TError(location, "ожидалось значение прагмы после ':'");
    }

    std::vector<std::string> values;
    comment = TrimLeft(comment);
    while (!comment.empty()) {
        auto value = ReadPragmaWord(comment);
        if (value.empty()) {
            throw TError(location, "пустое значение прагмы");
        }
        values.push_back(std::move(value));
        comment = TrimLeft(comment);
    }

    return TPragma{
        .Group = std::move(group),
        .Values = std::move(values),
        .Location = location,
    };
}

enum EState {
    Start,
    InNumber,
    InString,
    InIdentifier,
    InMaybeComment,
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
        Value += Unescape(ch);
    }
};

struct TIdentifierList {
    struct TWord {
        std::string Value;
        int ByteOffset = 0;
        int CharOffset = 0;
    };

    int ByteOffset = 0;
    int CharOffset = 0;
    std::vector<TWord> Words;

    bool Append(char ch) {
        auto byteOffset = ByteOffset;
        auto charOffset = CharOffset;
        Advance(ch);

        if (std::isspace(ch)) {
            if (!Words.empty() && !Words.back().Value.empty()) {
                Words.emplace_back();
                Words.back().ByteOffset = ByteOffset;
                Words.back().CharOffset = CharOffset;
            }
            return true;
        }
        if (Words.empty()) {
            Words.emplace_back();
            Words.back().ByteOffset = byteOffset;
            Words.back().CharOffset = charOffset;
        }
        if (std::isdigit(ch) && Words.back().Value.empty()) {
            // identifier cannot start with a digit
            return false;
        }
        Words.back().Value += ch;
        return true;
    }

    void Advance(char ch) {
        ByteOffset++;
        if (!IsUtf8ContinuationByte(ch)) {
            CharOffset++;
        }
    }
};

} // namespace

TTokenStream::TTokenStream(std::istream& in)
    : ITokenStream(in)
{ }

void TTokenStream::Read() {
    char ch = 0;
    char prev = 0; // for 2-char operators or current string quote
    std::variant<int64_t,double,TIdentifierList,TStringLiteral,std::monostate> token = std::monostate();
    std::string rawTokenValue;
    int frac = 10;
    bool repeat = false;
    bool unescape = false;
    TLocation tokenLocation = CurrentLocation;
    EState state = Start;
    // scientific notation state: 0 - none, 1 - just saw 'e'/'E' (expect sign or digits), 2 - in exponent digits
    int expMode = 0;
    int expSign = 1;
    int expValue = 0;
    int utf8bytesLeft = -1;
    std::string lineCommentValue;
    TLocation lineCommentLocation = CurrentLocation;

    auto finishLineComment = [&]() {
        auto pragma = ParsePragmaComment(lineCommentValue, lineCommentLocation);
        if (!pragma) {
            return;
        }
        if (SeenFirstToken) {
            throw TError(lineCommentLocation, "прагмы qumir должны идти до первого значимого токена");
        }
        Context.Pragmas.push_back(std::move(*pragma));
    };

    auto emitKeyword = [&](EKeyword kw, const std::string& rawValue) {
        Tokens.emplace_back(TToken {
            .Value = {.i64 = static_cast<int64_t>(kw)},
            .RawValue = rawValue,
            .Type = TToken::Keyword,
            .Location = tokenLocation
        });
    };

    auto emitOperator = [&](EOperator op, const std::string& rawValue) {
        Tokens.emplace_back(TToken {
            .Value = {.i64 = static_cast<int64_t>(op)},
            .RawValue = rawValue,
            .Type = TToken::Operator,
            .Location = tokenLocation
        });
    };

    auto emitIdentifier = [&](const std::string& name) {
        if (name.substr(0, 2) == "$$") {
            throw std::runtime_error("identifiers starting with '$$' are reserved");
        }
        if (Context.TypeNames.count(name)) {
            Tokens.emplace_back(TToken {
                .Value = {.i64 = static_cast<int64_t>(EKeyword::NamedType)},
                .Name = name,
                .RawValue = name,
                .Type = TToken::Keyword,
                .Location = tokenLocation
            });
        } else {
            Tokens.emplace_back(TToken {
                .Name = name,
                .RawValue = name,
                .Type = TToken::Identifier,
                .Location = tokenLocation
            });
        }
    };

    auto flush =[&]() {
        if (expMode != 0) {
            double baseVal = std::get<double>(token);
            if (expValue != 0 || expSign != 1) {
                baseVal *= std::pow(10.0, (double)expSign * (double)expValue);
            }
            token = baseVal;
        }

        if (std::holds_alternative<int64_t>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.i64 = std::get<int64_t>(token)},
                .RawValue = rawTokenValue,
                .Type = utf8bytesLeft == -1 ? TToken::Integer : TToken::Char,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<double>(token)) {
            Tokens.emplace_back(TToken {
                .Value = {.f64 = std::get<double>(token)},
                .RawValue = rawTokenValue,
                .Type = TToken::Float,
                .Location = tokenLocation
            });
        } else if (std::holds_alternative<TIdentifierList>(token)) {
            auto idList = std::get<TIdentifierList>(token);
            std::string logIdentifier;
            auto identLocation = tokenLocation;
            auto baseLocation = tokenLocation;
            for (const auto& wordItem : idList.Words) {
                const auto& word = wordItem.Value;
                int byteOffset = wordItem.ByteOffset;
                int charOffset = wordItem.CharOffset;
                auto wordLocation = TLocation {
                    .Line = baseLocation.Line,
                    .Byte = baseLocation.Byte + byteOffset,
                    .Column = baseLocation.Column + charOffset
                };
                if (word.empty()) {
                    continue;
                }
                auto maybeKw = KeywordMapRu.find(word);
                auto maybeOp = OperatorMap.find(word);
                auto maybeNamedType = Context.TypeNames.count(word);
                if (maybeNamedType) {
                    if (!logIdentifier.empty()) {
                        tokenLocation = identLocation;
                        emitIdentifier(logIdentifier);
                        logIdentifier.clear();
                    }
                    tokenLocation = wordLocation;
                    emitIdentifier(word);
                } else if (maybeKw != KeywordMapRu.end()) {
                    if (!logIdentifier.empty()) {
                        tokenLocation = identLocation;
                        emitIdentifier(logIdentifier);
                        logIdentifier.clear();
                    }
                    tokenLocation = wordLocation;
                    emitKeyword(maybeKw->second, word);
                } else if (maybeOp != OperatorMap.end()) {
                    if (!logIdentifier.empty()) {
                        tokenLocation = identLocation;
                        emitIdentifier(logIdentifier);
                        logIdentifier.clear();
                    }
                    tokenLocation = wordLocation;
                    emitOperator(maybeOp->second, word);
                } else {
                    if (logIdentifier.empty()) {
                        identLocation = wordLocation;
                    }
                    if (!logIdentifier.empty()) {
                        logIdentifier += " ";
                    }
                    logIdentifier += word;
                }
            }
            if (!logIdentifier.empty()) {
                tokenLocation = identLocation;
                emitIdentifier(logIdentifier);
            }
        } else if (std::holds_alternative<TStringLiteral>(token)) {
            const auto& strVal = std::get<TStringLiteral>(token).Value;
            if (auto chCode = AsSingleCharCode(strVal)) {
                Tokens.emplace_back(TToken {
                    .Value = {.i64 = *chCode},
                    .RawValue = strVal,
                    .Type = TToken::Char,
                    .Location = tokenLocation
                });
            } else {
                Tokens.emplace_back(TToken {
                    .Name = strVal,
                    .RawValue = strVal,
                    .Type = TToken::String,
                    .Location = tokenLocation
                });
            }
        }

        state = Start;
        repeat = true;
        unescape = false;
        tokenLocation = CurrentLocation;
        token = std::monostate();
        frac = 10;
        expMode = 0;
        expSign = 1;
        expValue = 0;
        utf8bytesLeft = -1;
        rawTokenValue.clear();
    };

    // single char operators:
    // /, +, -, =, ), [, ], ,,
    auto isSingleCharOperator = [](char ch) {
        return ch == '('
            || ch == ')'
            || ch == '+'
            || ch == '-'
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
        return isSingleCharOperator(ch) || isOperatorPrefix(ch) || ch == '(' || ch == '-' || ch == '"' || ch == '\n' || ch == ';' || ch == '|' || ch == '\'';
    };

    while ((Tokens.empty() || state != Start) && In.get(ch)) {
        AdvanceLocation(CurrentLocation, ch);

        do {
            repeat = false;
            // TODO: we don't support 'не' in the middle of an identifier so far
            // e.g. in 'true' kumir 'оно не истина' could be tokenized as 'не' 'оно истина'
            // if 'оно истина' is a variable name
            switch (state) {
                case Start:
                    if (ch == '\n' || ch == ';') {
                        emitOperator(EOperator::Eol, ch == '\n' ? "\\n" : std::string(1, ch));
                    } else if (std::isdigit(ch)) {
                        state = InNumber;
                        token = (int64_t)(ch - '0');
                        rawTokenValue += ch;
                    } else if (ch == '|') {
                        lineCommentValue.clear();
                        lineCommentLocation = CurrentLocation;
                        state = InLineComment;
                    } else if (ch == '.') {
                        state = InNumber;
                        token = (double)0.0;
                        rawTokenValue += '.';
                    } else if (ch == '\'' || ch == '"') {
                        token = TStringLiteral{};
                        state = InString;
                        // Remember the quote type to allow using the same char
                        prev = ch;
                    } else if (isOperatorPrefix(ch)) {
                        prev = ch;
                        state = InMaybeOperator; // reuse this state for 2-char operators
                    } else if (isSingleCharOperator(ch)) {
                        auto singleOpStr = std::string(1, ch);
                        emitOperator(OperatorMap.at(singleOpStr), singleOpStr);
                    } else if (!std::isspace(ch)) {
                        // identifiers/keywords: start with a letter, continue with letters, digits, underscores
                        TIdentifierList idList;
                        idList.Append(ch);
                        state = InIdentifier;
                        token = std::move(idList);
                    }
                    tokenLocation = CurrentLocation;
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
                    if (ch == prev) {
                        flush();
                        repeat = false; // need to skip '"'
                    } else if (ch == '\\') {
                        unescape = true;
                    } else {
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
                        emitOperator(it->second, opStr);
                        tokenLocation = CurrentLocation;
                        state = Start;
                    } else {
                        // not a 2-char operator, just the first char
                        auto singleOpStr = std::string(1, prev);
                        emitOperator(OperatorMap.at(singleOpStr), singleOpStr);
                        tokenLocation = CurrentLocation;
                        state = Start;
                        repeat = true;
                    }
                    break;
                }
                case InLineComment:
                    if (ch == '\n') {
                        finishLineComment();
                        state = Start;
                        repeat = true;
                    } else {
                        lineCommentValue += ch;
                    }
                    break;
                case InNumber:
                    if (expMode == 0) {
                        if (std::isdigit(ch)) {
                            rawTokenValue += ch;
                            if (std::holds_alternative<double>(token)) {
                                token = (double)(std::get<double>(token) * frac + (ch - '0')) / frac;
                                frac *= 10;
                            } else {
                                token = (int64_t)(std::get<int64_t>(token) * 10 + (ch - '0'));
                            }
                        } else if (ch == '.') {
                            rawTokenValue += ch;
                            if (std::holds_alternative<double>(token)) {
                                // Second dot in a number
                                flush();
                            } else {
                                token = (double)(std::get<int64_t>(token));
                            }
                        } else if (ch == 'E' || ch == 'e') {
                            rawTokenValue += ch;
                            // start exponent part
                            if (!std::holds_alternative<double>(token)) {
                                token = (double)(std::get<int64_t>(token));
                            }
                            expMode = 1;
                            expSign = 1;
                            expValue = 0;
                        } else {
                            flush();
                        }
                    } else {
                        // parsing exponent
                        if (std::isdigit(ch)) {
                            rawTokenValue += ch;
                            expMode = 2;
                            expValue = expValue * 10 + (ch - '0');
                        } else if (expMode == 1 && (ch == '+' || ch == '-')) {
                            rawTokenValue += ch;
                            expSign = (ch == '-') ? -1 : 1;
                            expMode = 2;
                        } else {
                            flush();
                        }
                    }
                    break;
                default:
                    throw std::runtime_error("invalid lexer state: " + std::to_string(state) + " at " + CurrentLocation.ToString() + " after '" + std::string(1, ch) + "'");
            };
        } while (repeat);
    }

    if (state == InLineComment) {
        finishLineComment();
    }
    flush();
}

} // namespace NAst
} // namespace NQumir
