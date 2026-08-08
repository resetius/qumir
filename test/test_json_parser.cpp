#include <gtest/gtest.h>

#include <qumir/parser/json/lexer.h>
#include <qumir/parser/json/node.h>
#include <qumir/parser/json/parser.h>

#include <algorithm>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace NQumir::NAst::NJson;

using NQumir::TError;
using NQumir::NAst::TToken;

namespace {

bool IsEofToken(const TToken& token) {
    return token.Type == TToken::Operator && token.Value.i64 == -1;
}

std::vector<TToken> Tokenize(const std::string& text) {
    std::istringstream in(text);
    TTokenStream stream(in);
    std::vector<TToken> tokens;
    while (true) {
        auto token = stream.Next();
        if (IsEofToken(token)) {
            break;
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

std::expected<TJson, TError> ParseJson(const std::string& text) {
    std::istringstream in(text);
    TTokenStream stream(in);
    TParser parser;
    return parser.Parse(stream);
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

#define ExpectOp(t, op) \
    do { \
        const auto& value = (t); \
        EXPECT_EQ(value.Type, TToken::Operator); \
        EXPECT_EQ(value.Value.i64, static_cast<int64_t>(op)); \
    } while (0)

#define ExpectString(t, v) \
    do { \
        const auto& value = (t); \
        EXPECT_EQ(value.Type, TToken::String); \
        EXPECT_EQ(value.Name, (v)); \
    } while (0)

#define ExpectKeyword(t, v) \
    do { \
        const auto& value = (t); \
        EXPECT_EQ(value.Type, TToken::Keyword); \
        EXPECT_EQ(value.Name, (v)); \
    } while (0)

#define ExpectInt(t, v) \
    do { \
        const auto& value = (t); \
        EXPECT_EQ(value.Type, TToken::Integer); \
        EXPECT_EQ(value.Value.i64, static_cast<int64_t>(v)); \
    } while (0)

#define ExpectFloat(t, v) \
    do { \
        const auto& value = (t); \
        EXPECT_EQ(value.Type, TToken::Float); \
        EXPECT_DOUBLE_EQ(value.Value.f64, (v)); \
    } while (0)

} // namespace

TEST(JsonLexer, EmptyInputYieldsEofImmediately) {
    EXPECT_TRUE(Tokenize("").empty());
    EXPECT_TRUE(Tokenize("   \n\t  ").empty());
}

TEST(JsonLexer, Punctuation) {
    auto tokens = Tokenize("{}[]:,");
    ASSERT_EQ(tokens.size(), 6u);
    ExpectOp(tokens[0], '{');
    ExpectOp(tokens[1], '}');
    ExpectOp(tokens[2], '[');
    ExpectOp(tokens[3], ']');
    ExpectOp(tokens[4], ':');
    ExpectOp(tokens[5], ',');
    EXPECT_EQ(tokens[0].RawValue, "{");
}

TEST(JsonLexer, Strings) {
    auto tokens = Tokenize(R"("" "abc" "a b")");
    ASSERT_EQ(tokens.size(), 3u);
    ExpectString(tokens[0], "");
    ExpectString(tokens[1], "abc");
    ExpectString(tokens[2], "a b");
    EXPECT_EQ(tokens[1].RawValue, "abc");
}

TEST(JsonLexer, StringEscapes) {
    auto tokens = Tokenize(R"("a\nb\tc\\d\"e")");
    ASSERT_EQ(tokens.size(), 1u);
    ExpectString(tokens[0], "a\nb\tc\\d\"e");
    EXPECT_EQ(tokens[0].RawValue, R"(a\nb\tc\\d\"e)");
}

TEST(JsonLexer, StringKeepsUtf8Bytes) {
    auto tokens = Tokenize(R"("ключ")");
    ASSERT_EQ(tokens.size(), 1u);
    ExpectString(tokens[0], "ключ");
}

TEST(JsonLexer, Integers) {
    auto tokens = Tokenize("0 42 -7");
    ASSERT_EQ(tokens.size(), 3u);
    ExpectInt(tokens[0], 0);
    ExpectInt(tokens[1], 42);
    ExpectInt(tokens[2], -7);
}

TEST(JsonLexer, Floats) {
    auto tokens = Tokenize("1.5 -0.25 1e3 2.5E-2 1E+2");
    ASSERT_EQ(tokens.size(), 5u);
    ExpectFloat(tokens[0], 1.5);
    ExpectFloat(tokens[1], -0.25);
    ExpectFloat(tokens[2], 1e3);
    ExpectFloat(tokens[3], 2.5e-2);
    ExpectFloat(tokens[4], 1e2);
}

TEST(JsonLexer, Keywords) {
    auto tokens = Tokenize("true false null");
    ASSERT_EQ(tokens.size(), 3u);
    ExpectKeyword(tokens[0], "true");
    ExpectKeyword(tokens[1], "false");
    ExpectKeyword(tokens[2], "null");
}

TEST(JsonLexer, TracksLocations) {
    auto tokens = Tokenize("{\n  \"a\": 1\n}");
    ASSERT_EQ(tokens.size(), 5u);

    EXPECT_EQ(tokens[0].Location.Line, 1);
    EXPECT_EQ(tokens[0].Location.Column, 1);

    ExpectString(tokens[1], "a");
    EXPECT_EQ(tokens[1].Location.Line, 2);
    EXPECT_EQ(tokens[1].Location.Column, 3);

    ExpectOp(tokens[2], ':');
    EXPECT_EQ(tokens[2].Location.Line, 2);
    EXPECT_EQ(tokens[2].Location.Column, 6);

    ExpectInt(tokens[3], 1);
    EXPECT_EQ(tokens[3].Location.Line, 2);
    EXPECT_EQ(tokens[3].Location.Column, 8);

    ExpectOp(tokens[4], '}');
    EXPECT_EQ(tokens[4].Location.Line, 3);
    EXPECT_EQ(tokens[4].Location.Column, 1);
}

TEST(JsonLexer, TracksColumnsOnSingleLine) {
    auto tokens = Tokenize(R"({"ab": 12, "c": true})");
    ASSERT_EQ(tokens.size(), 9u);

    const int expected[] = {1, 2, 6, 8, 10, 12, 15, 17, 21};
    for (size_t i = 0; i < tokens.size(); ++i) {
        EXPECT_EQ(tokens[i].Location.Line, 1);
        EXPECT_EQ(tokens[i].Location.Column, expected[i])
            << "token " << i << " raw='" << tokens[i].RawValue << "'";
    }
}

TEST(JsonLexer, UngetReturnsToken) {
    std::istringstream in("[1]");
    TTokenStream stream(in);

    auto first = stream.Next();
    ExpectOp(first, '[');
    stream.Unget(first);

    ExpectOp(stream.Next(), '[');
    ExpectInt(stream.Next(), 1);
    ExpectOp(stream.Next(), ']');
    EXPECT_TRUE(IsEofToken(stream.Next()));
}

TEST(JsonLexer, EofIsIdempotent) {
    std::istringstream in("1");
    TTokenStream stream(in);
    ExpectInt(stream.Next(), 1);
    EXPECT_TRUE(IsEofToken(stream.Next()));
    EXPECT_TRUE(IsEofToken(stream.Next()));
}

TEST(JsonLexer, UnknownIdentifierThrows) {
    EXPECT_THROW(Tokenize("nil"), TError);
    EXPECT_THROW(Tokenize("True"), TError);
}

TEST(JsonLexer, UnexpectedCharacterThrows) {
    EXPECT_THROW(Tokenize("@"), TError);
    EXPECT_THROW(Tokenize("'x'"), TError);
}

TEST(JsonLexer, UnterminatedStringThrows) {
    EXPECT_THROW(Tokenize("\"abc"), TError);
}

TEST(JsonLexer, UnsupportedEscapeThrows) {
    // \u, \r, \b, \f are not handled by Unescape()
    EXPECT_THROW(Tokenize("\"\\u0041\""), TError);
    EXPECT_THROW(Tokenize("\"\\r\""), TError);
}

TEST(JsonLexer, ExponentWithoutDigitsThrows) {
    EXPECT_THROW(Tokenize("1e"), TError);
    EXPECT_THROW(Tokenize("1e+"), TError);
}

TEST(JsonParser, ParsesScalars) {
    {
        auto json = ParseJson("42");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* value = json->Root()->As<TInteger>();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value->Value, 42);
    }
    {
        auto json = ParseJson("-1.5");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* value = json->Root()->As<TFloat>();
        ASSERT_NE(value, nullptr);
        EXPECT_DOUBLE_EQ(value->Value, -1.5);
    }
    {
        auto json = ParseJson(R"("hi")");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* value = json->Root()->As<TString>();
        ASSERT_NE(value, nullptr);
        EXPECT_EQ(value->Value, "hi");
    }
    {
        auto json = ParseJson("true");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* value = json->Root()->As<TBoolean>();
        ASSERT_NE(value, nullptr);
        EXPECT_TRUE(value->Value);
    }
    {
        auto json = ParseJson("false");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* value = json->Root()->As<TBoolean>();
        ASSERT_NE(value, nullptr);
        EXPECT_FALSE(value->Value);
    }
    {
        auto json = ParseJson("null");
        ASSERT_TRUE(json) << json.error().ToString();
        EXPECT_EQ(json->Root()->ValueType(), EType::Null);
        EXPECT_NE(json->Root()->As<TNull>(), nullptr);
    }
}

TEST(JsonParser, ParsesEmptyContainers) {
    {
        auto json = ParseJson("{}");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* object = json->Root()->As<TObject>();
        ASSERT_NE(object, nullptr);
        EXPECT_TRUE(object->Members.empty());
    }
    {
        auto json = ParseJson("[]");
        ASSERT_TRUE(json) << json.error().ToString();
        auto* array = json->Root()->As<TArray>();
        ASSERT_NE(array, nullptr);
        EXPECT_TRUE(array->Elements.empty());
    }
}

TEST(JsonParser, ParsesFlatArray) {
    auto json = ParseJson(R"([1, 2.5, "x", true, null])");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* array = json->Root()->As<TArray>();
    ASSERT_NE(array, nullptr);
    ASSERT_EQ(array->Elements.size(), 5u);

    ASSERT_NE(array->Elements[0]->As<TInteger>(), nullptr);
    EXPECT_EQ(array->Elements[0]->As<TInteger>()->Value, 1);
    ASSERT_NE(array->Elements[1]->As<TFloat>(), nullptr);
    EXPECT_DOUBLE_EQ(array->Elements[1]->As<TFloat>()->Value, 2.5);
    ASSERT_NE(array->Elements[2]->As<TString>(), nullptr);
    EXPECT_EQ(array->Elements[2]->As<TString>()->Value, "x");
    ASSERT_NE(array->Elements[3]->As<TBoolean>(), nullptr);
    EXPECT_TRUE(array->Elements[3]->As<TBoolean>()->Value);
    EXPECT_EQ(array->Elements[4]->ValueType(), EType::Null);
}

TEST(JsonParser, ParsesNestedStructures) {
    auto json = ParseJson(R"({
        "name": "qumir",
        "nested": {"deep": [1, {"k": "v"}]},
        "empty": {}
    })");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* root = json->Root()->As<TObject>();
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->Members.size(), 3u);

    auto* name = root->Get("name");
    ASSERT_NE(name, nullptr);
    ASSERT_NE(name->As<TString>(), nullptr);
    EXPECT_EQ(name->As<TString>()->Value, "qumir");

    auto* nested = root->Get("nested");
    ASSERT_NE(nested, nullptr);
    auto* nestedObject = nested->As<TObject>();
    ASSERT_NE(nestedObject, nullptr);

    auto* deep = nestedObject->Get("deep");
    ASSERT_NE(deep, nullptr);
    auto* deepArray = deep->As<TArray>();
    ASSERT_NE(deepArray, nullptr);
    ASSERT_EQ(deepArray->Elements.size(), 2u);
    ASSERT_NE(deepArray->Elements[0]->As<TInteger>(), nullptr);
    EXPECT_EQ(deepArray->Elements[0]->As<TInteger>()->Value, 1);

    auto* inner = deepArray->Elements[1]->As<TObject>();
    ASSERT_NE(inner, nullptr);
    ASSERT_NE(inner->Get("k"), nullptr);
    EXPECT_EQ(inner->Get("k")->As<TString>()->Value, "v");

    auto* empty = root->Get("empty");
    ASSERT_NE(empty, nullptr);
    ASSERT_NE(empty->As<TObject>(), nullptr);
    EXPECT_TRUE(empty->As<TObject>()->Members.empty());
}

TEST(JsonParser, ParsesDeeplyNestedArrays) {
    auto json = ParseJson("[[[[[1]]]]]");
    ASSERT_TRUE(json) << json.error().ToString();

    IValue* current = json->Root();
    for (int depth = 0; depth < 5; ++depth) {
        auto* array = current->As<TArray>();
        ASSERT_NE(array, nullptr) << "depth " << depth;
        ASSERT_EQ(array->Elements.size(), 1u);
        current = array->Elements[0];
    }
    ASSERT_NE(current->As<TInteger>(), nullptr);
    EXPECT_EQ(current->As<TInteger>()->Value, 1);
}

TEST(JsonObject, MembersAreSorted) {
    auto json = ParseJson(R"({"c": 3, "a": 1, "b": 2})");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* object = json->Root()->As<TObject>();
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(object->Members.size(), 3u);
    EXPECT_EQ(object->Members[0].first, "a");
    EXPECT_EQ(object->Members[1].first, "b");
    EXPECT_EQ(object->Members[2].first, "c");

    EXPECT_EQ(object->Get("a")->As<TInteger>()->Value, 1);
    EXPECT_EQ(object->Get("b")->As<TInteger>()->Value, 2);
    EXPECT_EQ(object->Get("c")->As<TInteger>()->Value, 3);
}

TEST(JsonObject, GetMissingKeyReturnsNull) {
    auto json = ParseJson(R"({"a": 1})");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* object = json->Root()->As<TObject>();
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->Get("b"), nullptr);
    EXPECT_EQ(object->Get(""), nullptr);
}

TEST(JsonObject, GetOnEmptyObjectReturnsNull) {
    auto json = ParseJson("{}");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* object = json->Root()->As<TObject>();
    ASSERT_NE(object, nullptr);
    EXPECT_EQ(object->Get("a"), nullptr);

    auto [first, last] = object->GetRange("a");
    EXPECT_EQ(first, last);
}

TEST(JsonObject, GetRangeCoversDuplicateKeys) {
    auto json = ParseJson(R"({"a": 1, "b": 2, "a": 3})");
    ASSERT_TRUE(json) << json.error().ToString();

    auto* object = json->Root()->As<TObject>();
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(object->Members.size(), 3u);

    auto [first, last] = object->GetRange("a");
    ASSERT_EQ(std::distance(first, last), 2);

    std::vector<int64_t> values;
    for (auto it = first; it != last; ++it) {
        EXPECT_EQ(it->first, "a");
        ASSERT_NE(it->second->As<TInteger>(), nullptr);
        values.push_back(it->second->As<TInteger>()->Value);
    }
    std::sort(values.begin(), values.end());
    EXPECT_EQ(values, (std::vector<int64_t>{1, 3}));

    auto [bFirst, bLast] = object->GetRange("b");
    ASSERT_EQ(std::distance(bFirst, bLast), 1);
    EXPECT_EQ(bFirst->second->As<TInteger>()->Value, 2);

    auto [missingFirst, missingLast] = object->GetRange("z");
    EXPECT_EQ(missingFirst, missingLast);
}

TEST(JsonNode, AsReturnsNullForWrongType) {
    auto json = ParseJson(R"({"a": 1})");
    ASSERT_TRUE(json) << json.error().ToString();

    IValue* root = json->Root();
    ASSERT_NE(root, nullptr);
    EXPECT_NE(root->As<TObject>(), nullptr);
    EXPECT_EQ(root->As<TArray>(), nullptr);
    EXPECT_EQ(root->As<TInteger>(), nullptr);
    EXPECT_EQ(root->As<TString>(), nullptr);
    EXPECT_EQ(root->As<TNull>(), nullptr);
}

TEST(JsonNode, DefaultConstructedJsonHasNoRoot) {
    TJson json;
    EXPECT_EQ(json.Root(), nullptr);
}

TEST(JsonNode, MoveKeepsNodesAlive) {
    auto parsed = ParseJson(R"({"a": "value"})");
    ASSERT_TRUE(parsed) << parsed.error().ToString();

    TJson moved = std::move(*parsed);
    EXPECT_EQ(parsed->Root(), nullptr);

    auto* object = moved.Root()->As<TObject>();
    ASSERT_NE(object, nullptr);
    ASSERT_NE(object->Get("a"), nullptr);
    EXPECT_EQ(object->Get("a")->As<TString>()->Value, "value");

    TJson assigned;
    assigned = std::move(moved);
    EXPECT_EQ(moved.Root(), nullptr);
    ASSERT_NE(assigned.Root(), nullptr);
    EXPECT_EQ(assigned.Root()->As<TObject>()->Get("a")->As<TString>()->Value, "value");
}

TEST(JsonParser, RejectsEmptyInput) {
    auto json = ParseJson("");
    ASSERT_FALSE(json.has_value());
    EXPECT_TRUE(Contains(json.error().ToString(), "unexpected operator"));
}

TEST(JsonParser, RejectsTrailingContent) {
    auto json = ParseJson("1 2");
    ASSERT_FALSE(json.has_value());
    EXPECT_TRUE(Contains(json.error().ToString(), "unexpected token after JSON value"));

    EXPECT_FALSE(ParseJson("{} []").has_value());
    EXPECT_FALSE(ParseJson("true false").has_value());
}

TEST(JsonParser, RejectsMalformedObjects) {
    struct TCase {
        const char* Input;
        const char* Message;
    };

    const TCase cases[] = {
        {R"({"a" 1})", "expected ':' after key in object"},
        {R"({1: 2})", "expected string key in object"},
        {R"({"a": 1,})", "expected string key in object"},
        {R"({"a": 1 "b": 2})", "expected ',' or '}' in object"},
        {R"({"a": 1)", "expected ',' or '}' in object"},
        {R"({"a")", "expected ':' after key in object"},
        {R"({"a":})", "unexpected operator"},
    };

    for (const auto& c : cases) {
        auto json = ParseJson(c.Input);
        ASSERT_FALSE(json.has_value()) << c.Input;
        EXPECT_TRUE(Contains(json.error().ToString(), c.Message))
            << c.Input << " -> " << json.error().ToString();
    }
}

TEST(JsonParser, RejectsMalformedArrays) {
    struct TCase {
        const char* Input;
        const char* Message;
    };

    const TCase cases[] = {
        {"[1, 2", "expected ',' or ']'"},
        {"[1 2]", "expected ',' or ']'"},
        {"[1,]", "unexpected operator"},
        {"[,]", "unexpected operator"},
        {"[", "unexpected operator"},
    };

    for (const auto& c : cases) {
        auto json = ParseJson(c.Input);
        ASSERT_FALSE(json.has_value()) << c.Input;
        EXPECT_TRUE(Contains(json.error().ToString(), c.Message))
            << c.Input << " -> " << json.error().ToString();
    }
}

TEST(JsonParser, LexerErrorsInsideValueBecomeParseErrors) {
    auto json = ParseJson("[1, @]");
    ASSERT_FALSE(json.has_value());
    EXPECT_TRUE(Contains(json.error().ToString(), "unexpected character '@'"))
        << json.error().ToString();

    auto identifier = ParseJson(R"({"a": nil})");
    ASSERT_FALSE(identifier.has_value());
    EXPECT_TRUE(Contains(identifier.error().ToString(), "unexpected identifier 'nil'"))
        << identifier.error().ToString();
}

TEST(JsonParser, LexerErrorAfterTopLevelValueIsReportedAsError) {
    auto json = ParseJson("1 @");
    ASSERT_FALSE(json.has_value());
    EXPECT_TRUE(Contains(json.error().ToString(), "unexpected character '@'"))
        << json.error().ToString();

    auto identifier = ParseJson("1 nil");
    ASSERT_FALSE(identifier.has_value());
    EXPECT_TRUE(Contains(identifier.error().ToString(), "unexpected identifier 'nil'"))
        << identifier.error().ToString();
}

TEST(JsonParser, ReportsErrorLocation) {
    auto json = ParseJson("{\n  \"a\" 1\n}");
    ASSERT_FALSE(json.has_value());

    auto message = json.error().ToString();
    EXPECT_TRUE(Contains(message, "Line: 2")) << message;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
