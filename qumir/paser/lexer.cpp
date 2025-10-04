#include "lexer.h"

#include <map>

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
    {"div", EKeyword::Div},
    {"mod", EKeyword::Mod},
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

} // namespace NAst
} // namespace NQumir
