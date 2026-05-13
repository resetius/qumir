#include <gtest/gtest.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/operator.h>

#include <sstream>

using namespace NQumir::NAst::NCore;
using namespace NQumir::NAst::NLiterals;

using NQumir::NAst::TWrappedTokenStream;
using NQumir::NAst::TToken;

namespace {

#define ExpectOp(t, op) \
    do { \
        auto value = (t); \
        EXPECT_EQ(value.Type, TToken::Operator); \
        EXPECT_EQ(value.Value.i64, static_cast<int64_t>(op)); \
    } while (0)

#define ExpectIdent(t, name) \
    do { \
        auto value = (t); \
        EXPECT_EQ(value.Type, TToken::Identifier); \
        EXPECT_EQ(value.Name, (name)); \
    } while (0)

#define ExpectInt(t, v) \
    do { \
        auto value = (t); \
        EXPECT_EQ(value.Type, TToken::Integer); \
        EXPECT_EQ(value.Value.i64, (v)); \
    } while (0)

#define ExpectFloat(t, v) \
    do { \
        auto value = (t); \
        EXPECT_EQ(value.Type, TToken::Float); \
        EXPECT_DOUBLE_EQ(value.Value.f64, (v)); \
    } while (0)

#define ExpectBool(t, v) \
    do { \
        auto value = (t); \
        EXPECT_EQ(value.Type, TToken::Boolean); \
        EXPECT_EQ(value.Value.i64, (v)); \
    } while (0)

} // namespace

TEST(CoreLexerTest, FormsAndCompositeTypes) {
    std::istringstream input("(let (x 1) (if #t x 0)):<named color i32>");
    TTokenStream tokens(input);

    ExpectOp(tokens.Next(), "("_op);
    ExpectIdent(tokens.Next(), "let");
    ExpectOp(tokens.Next(), "("_op);
    ExpectIdent(tokens.Next(), "x");
    ExpectInt(tokens.Next(), 1);
    ExpectOp(tokens.Next(), ")"_op);
    ExpectOp(tokens.Next(), "("_op);
    ExpectIdent(tokens.Next(), "if");
    ExpectBool(tokens.Next(), 1);
    ExpectIdent(tokens.Next(), "x");
    ExpectInt(tokens.Next(), 0);
    ExpectOp(tokens.Next(), ")"_op);
    ExpectOp(tokens.Next(), ")"_op);
    ExpectOp(tokens.Next(), ":"_op);
    ExpectOp(tokens.Next(), "<"_op);
    ExpectIdent(tokens.Next(), "named");
    ExpectIdent(tokens.Next(), "color");
    ExpectIdent(tokens.Next(), "i32");
    ExpectOp(tokens.Next(), ">"_op);
    ExpectOp(tokens.Next(), NQumir::NAst::TOperator((uint64_t)-1));
}

TEST(CoreLexerTest, LiteralsAndBarIdentifiers) {
    std::istringstream input(R"(|foo bar| 42 1.25 .5 3e-2 "a\nb" '\n' #f)");
    TTokenStream tokens(input);

    ExpectIdent(tokens.Next(), "foo bar");
    ExpectInt(tokens.Next(), 42);
    ExpectFloat(tokens.Next(), 1.25);
    ExpectFloat(tokens.Next(), .5);
    ExpectFloat(tokens.Next(), 3e-2);

    auto str = tokens.Next();
    EXPECT_EQ(str.Type, TToken::String);
    EXPECT_EQ(str.Name, std::string("a\nb"));

    auto ch = tokens.Next();
    EXPECT_EQ(ch.Type, TToken::Char);
    EXPECT_EQ(ch.Value.i64, '\n');

    ExpectBool(tokens.Next(), 0);
    ExpectOp(tokens.Next(), NQumir::NAst::TOperator((uint64_t)-1));
}

TEST(CoreWrappedTokenStreamTest, Window) {
    std::istringstream input("(block x)");
    TTokenStream base(input);
    TWrappedTokenStream tokens(base, 2);

    ExpectOp(tokens.Next(), "("_op);
    ExpectIdent(tokens.Next(), "block");
    ExpectIdent(tokens.Next(), "x");

    ASSERT_EQ(tokens.GetWindow().size(), 2);
    ExpectIdent(tokens.GetWindow()[0], "block");
    ExpectIdent(tokens.GetWindow()[1], "x");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
