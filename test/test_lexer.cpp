#include <gtest/gtest.h>
#include <qumir/parser/lexer.h>
#include <sstream>

using namespace NQumir::NAst;

namespace {

// --- helpers ----------------------------------------------------------------
#define ExpectKeyword(t, kw) \
    do { \
        auto value = (t); \
        ASSERT_TRUE(value.has_value()); \
        EXPECT_EQ(value->Type, TToken::Keyword); \
        EXPECT_EQ(value->Value.i64, (int64_t)(kw)); \
    } while(0);

#define ExpectOp(t, op) \
    do { \
        auto value = (t); \
        ASSERT_TRUE(value.has_value()); \
        EXPECT_EQ(value->Type, TToken::Operator); \
        EXPECT_EQ(value->Value.i64, (int64_t)(op)); \
    } while(0);

#define ExpectIdent(t, name) \
    do { \
        auto value = (t); \
        ASSERT_TRUE(value.has_value()); \
        EXPECT_EQ(value->Type, TToken::Identifier); \
        EXPECT_EQ(value->Name, (name)); \
    } while(0);

#define ExpectInt(t, v) \
    do { \
        auto value = (t); \
        ASSERT_TRUE(value.has_value()); \
        EXPECT_EQ(value->Type, TToken::Integer); \
        EXPECT_EQ(value->Value.i64, (v)); \
    } while(0);

#define ExpectFloat(t, v) \
    do { \
        auto value = (t); \
        ASSERT_TRUE(value.has_value()); \
        EXPECT_EQ(value->Type, TToken::Float); \
        EXPECT_EQ(value->Value.f64, (v)); \
    } while(0);

} // namespace

TEST(LexerTest, Numbers) {
    std::istringstream input("42 + 23");
    TTokenStream tokens(input);

    ExpectInt(tokens.Next(), 42);
    ExpectOp(tokens.Next(), EOperator::Plus);
    ExpectInt(tokens.Next(), 23);
}

TEST(LexerTest, NegativeInt) {
    std::istringstream input("-1");
    TTokenStream tokens(input);

    ExpectInt(tokens.Next(), -1);
}

TEST(LexerTest, NegativeFloat) {
    std::istringstream input("-.1");
    TTokenStream tokens(input);

    ExpectFloat(tokens.Next(), -.1);
}

TEST(LexerTest, Assignment) {
    std::istringstream input("x := 23");
    TTokenStream tokens(input);

    ExpectIdent(tokens.Next(), "x");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 23);
}

// --- multi-word identifiers --------------------------------------------------
TEST(LexerTest, MultiWordIdentifierDeclarationAndUse) {
    std::istringstream input(
        "цел длина отрезка\n"
        "длина отрезка := 5\n"
    );
    TTokenStream tokens(input);

    // "цел длина отрезка"
    ExpectKeyword(tokens.Next(), EKeyword::Int);
    ExpectIdent(tokens.Next(), "длина отрезка");
    ExpectOp(tokens.Next(), EOperator::Eol);

    // "длина отрезка := 5"
    ExpectIdent(tokens.Next(), "длина отрезка");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 5);
}

// --- 'не' as identifier on LHS, and as operator in expression ---------------
TEST(LexerTest, NotAsIdentifierAndOperator) {
    std::istringstream input(
        "лог не\n"
        "не := ложь\n"
        "если не x то\n"
    );
    TTokenStream tokens(input);

    // "лог не"
    ExpectKeyword(tokens.Next(), EKeyword::Bool);
    ExpectOp(tokens.Next(), EOperator::Not);
    ExpectOp(tokens.Next(), EOperator::Eol);

    // "не := ложь"
    ExpectOp(tokens.Next(), EOperator::Not);
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectKeyword(tokens.Next(), EKeyword::False);
    ExpectOp(tokens.Next(), EOperator::Eol);

    // "если не x то"
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectOp(tokens.Next(), EOperator::Not); // 'не' как оператор
    ExpectIdent(tokens.Next(), "x");
    ExpectKeyword(tokens.Next(), EKeyword::Then);
}

// --- 'не готов' (multi-word starting with 'не ') ----------------------------
TEST(LexerTest, NotWordChainAsLhsAndInExpr) {
    std::istringstream input(
        "лог не готов\n"
        "не готов := истина\n"
        "если не готов то\n"
    );
    TTokenStream tokens(input);

    // Declaration "лог не готов"
    ExpectKeyword(tokens.Next(), EKeyword::Bool);
    ExpectOp(tokens.Next(), EOperator::Not);
    ExpectIdent(tokens.Next(), "готов");
    ExpectOp(tokens.Next(), EOperator::Eol);

    // Assignment "не готов := истина"
    ExpectOp(tokens.Next(), EOperator::Not);
    ExpectIdent(tokens.Next(), "готов");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectKeyword(tokens.Next(), EKeyword::True);
    ExpectOp(tokens.Next(), EOperator::Eol);

    // Expression "если не готов то"  (в Expr наш фильтр трактует как NOT + IDENT("готов"))
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectOp(tokens.Next(), EOperator::Not);
    ExpectIdent(tokens.Next(), "готов");
    ExpectKeyword(tokens.Next(), EKeyword::Then);
}

// --- keywords inside identifiers on LHS -------------------------------------
TEST(LexerTest, KeywordsInsideIdentifierLhs) {
    std::istringstream input("цел если число\nесли число := 10\n");
    TTokenStream tokens(input);
    ExpectKeyword(tokens.Next(), EKeyword::Int);
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectIdent(tokens.Next(), "число");
    ExpectOp(tokens.Next(), EOperator::Eol);
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectIdent(tokens.Next(), "число");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 10);
}

// --- ввод/вывод lists expect identifiers after commas -----------------------
TEST(LexerTest, InputOutputListsWithMultiWordIdentifiers) {
    std::istringstream input(
        "ввод длина отрезка, ширина прямоугольника\n"
        "вывод длина отрезка, нс\n"
    );
    TTokenStream tokens(input);
    // ввод ...
    ExpectKeyword(tokens.Next(), EKeyword::Input);
    ExpectIdent(tokens.Next(), "длина отрезка");
    ExpectOp(tokens.Next(), EOperator::Comma);
    ExpectIdent(tokens.Next(), "ширина прямоугольника");
    ExpectOp(tokens.Next(), EOperator::Eol);
    // вывод ...
    ExpectKeyword(tokens.Next(), EKeyword::Output);
    ExpectIdent(tokens.Next(), "длина отрезка");
    ExpectOp(tokens.Next(), EOperator::Comma);
    // "нс" как keyword переноса строки (если у тебя так заведено) — иначе можно ожидать Identifier("нс")
    auto t = tokens.Next();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->Type, TToken::Keyword) << "ожидался keyword 'нс'";
    ExpectOp(tokens.Next(), EOperator::Eol);
}

TEST(LexerTest, DivModOperatorVsIdentifier) {
    // как оператор
    {
        std::istringstream input("10 div 3, 10 mod 3");
        TTokenStream tokens(input);
        ExpectInt(tokens.Next(), 10);
        ExpectOp(tokens.Next(), EOperator::Div);
        ExpectInt(tokens.Next(), 3);
        ExpectOp(tokens.Next(), EOperator::Comma);
        ExpectInt(tokens.Next(), 10);
        ExpectOp(tokens.Next(), EOperator::Mod);
        ExpectInt(tokens.Next(), 3);
    }
    // как имя на LHS
    {
        std::istringstream input("div := 5\nmod := 7\n");
        TTokenStream tokens(input);
        ExpectOp(tokens.Next(), EOperator::Div);
        ExpectOp(tokens.Next(), EOperator::Assign);
        ExpectInt(tokens.Next(), 5);
        ExpectOp(tokens.Next(), EOperator::Eol);
        ExpectOp(tokens.Next(), EOperator::Mod);
        ExpectOp(tokens.Next(), EOperator::Assign);
        ExpectInt(tokens.Next(), 7);
    }
}

// --- Indexing and brackets ---------------------------------------------------
TEST(LexerTest, IndexingBrackets) {
    std::istringstream input("t[i] := 1");
    TTokenStream tokens(input);
    ExpectIdent(tokens.Next(), "t");
    ExpectOp(tokens.Next(), EOperator::LSqBr);
    ExpectIdent(tokens.Next(), "i");
    ExpectOp(tokens.Next(), EOperator::RSqBr);
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 1);
}

// --- Two-char operators and comparisons -------------------------------------
TEST(LexerTest, TwoCharOperators) {
    std::istringstream input("a <= b, c >= d, e <> f, x ** 2");
    TTokenStream tokens(input);
    ExpectIdent(tokens.Next(), "a");
    ExpectOp(tokens.Next(), EOperator::Leq);
    ExpectIdent(tokens.Next(), "b");
    ExpectOp(tokens.Next(), EOperator::Comma);
    ExpectIdent(tokens.Next(), "c");
    ExpectOp(tokens.Next(), EOperator::Geq);
    ExpectIdent(tokens.Next(), "d");
    ExpectOp(tokens.Next(), EOperator::Comma);
    ExpectIdent(tokens.Next(), "e");
    ExpectOp(tokens.Next(), EOperator::Neq);
    ExpectIdent(tokens.Next(), "f");
    ExpectOp(tokens.Next(), EOperator::Comma);
    ExpectIdent(tokens.Next(), "x");
    ExpectOp(tokens.Next(), EOperator::Pow);
    ExpectInt(tokens.Next(), 2);
}

// --- Strings and comments ----------------------------------------------------
TEST(LexerTest, StringLiteralAndComments) {
    std::istringstream input(
        "вывод \"Привет\", нс  (* комментарий *)\n"
        "-- ещё комментарий до конца строки\n"
        "вывод \"OK\""
    );
    TTokenStream tokens(input);

    ExpectKeyword(tokens.Next(), EKeyword::Output);
    auto t = tokens.Next(); // "Привет"
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->Type, TToken::String);
    EXPECT_EQ(t->Name, std::string("Привет"));
    ExpectOp(tokens.Next(), EOperator::Comma);
    // "нс" — как keyword (или идентификатор — см. реализацию)
    t = tokens.Next();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->Type, TToken::Keyword) << "ожидался keyword 'нс'";
    ExpectOp(tokens.Next(), EOperator::Eol);

    // комментарии должны быть съедены, далее снова "вывод"
    ExpectKeyword(tokens.Next(), EKeyword::Output);
    t = tokens.Next();
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->Type, TToken::String);
    EXPECT_EQ(t->Name, std::string("OK"));
}

TEST(LexerTest, EolBetweenStatements) {
    std::istringstream input("x := 1\ny := 2");
    TTokenStream tokens(input);
    ExpectIdent(tokens.Next(), "x");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 1);
    ExpectOp(tokens.Next(), EOperator::Eol); // \n
    ExpectIdent(tokens.Next(), "y");
    ExpectOp(tokens.Next(), EOperator::Assign);
    ExpectInt(tokens.Next(), 2);
}

// --- "иначе если" should be two keywords in control flow --------------------
TEST(LexerTest, ElseIfAsTwoKeywords) {
    std::istringstream input("если x то\nиначе если y то\nвсе\n");
    TTokenStream tokens(input);
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectIdent(tokens.Next(), "x");
    ExpectKeyword(tokens.Next(), EKeyword::Then);
    ExpectOp(tokens.Next(), EOperator::Eol);
    ExpectKeyword(tokens.Next(), EKeyword::Else);
    ExpectKeyword(tokens.Next(), EKeyword::If);
    ExpectIdent(tokens.Next(), "y");
    ExpectKeyword(tokens.Next(), EKeyword::Then);
    ExpectOp(tokens.Next(), EOperator::Eol);
    ExpectKeyword(tokens.Next(), EKeyword::Break);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
