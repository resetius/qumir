#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/semantics/type_annotation/type_annotation.h>

#include <gtest/gtest.h>

#include <sstream>

using namespace NQumir;
using namespace NQumir::NAst;

namespace {

std::expected<TExprPtr, TError> Annotate(const std::string& source) {
    std::istringstream input(source);
    NCore::TTokenStream tokens(input);
    auto parsed = NCore::TParser{}.Parse(tokens);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    NSemantics::TNameResolver resolver;
    if (auto error = resolver.Resolve(*parsed)) {
        return std::unexpected(*error);
    }
    return NTypeAnnotation::TTypeAnnotator(resolver).Annotate(*parsed);
}

std::expected<TExprPtr, TError> AnnotateBinary(
    const std::string& op,
    const std::string& left,
    const std::string& right)
{
    auto root = Annotate("(block (var a " + left + ") (var b " + right
        + ") (var result = (" + op + " a b)))");
    if (!root) {
        return root;
    }
    return TMaybeNode<TVarStmt>(TMaybeNode<TBlockExpr>(*root).Cast()->Stmts.back()).Cast()->Init;
}

std::string TypeName(const TTypePtr& type) {
    return TypeKey(UnwrapReferenceType(type));
}

} // namespace

TEST(BinaryTypeAnnotation, NumericVectorPromotionAndDivision) {
    struct TCase {
        std::string Op;
        std::string Left;
        std::string Right;
        std::string Result;
        std::string LeftOperand;
        std::string RightOperand;
    };
    const std::vector<TCase> cases = {
        {"+", "<vec i32 4>", "<vec i32 4>", "Vector::i32::4", "Vector::i32::4", "Vector::i32::4"},
        {"+", "<vec i32 4>", "<vec f64 4>", "Vector::Float::4", "Vector::Float::4", "Vector::Float::4"},
        {"+", "<vec f64 4>", "<vec i32 4>", "Vector::Float::4", "Vector::Float::4", "Vector::Float::4"},
        {"*", "<vec i32 4>", "i64", "Vector::i64::4", "Vector::i64::4", "i64"},
        {"+", "<vec i32 4>", "f64", "Vector::Float::4", "Vector::Float::4", "Float"},
        {"-", "i32", "<vec i32 4>", "Vector::i32::4", "i32", "Vector::i32::4"},
        {"/", "<vec i32 4>", "<vec i32 4>", "Vector::Float::4", "Vector::Float::4", "Vector::Float::4"},
        {"/", "i32", "<vec i32 4>", "Vector::Float::4", "Float", "Vector::Float::4"},
        {"//", "<vec i32 4>", "<vec i32 4>", "Vector::i32::4", "Vector::i32::4", "Vector::i32::4"},
        {"%", "<vec i32 4>", "i32", "Vector::i32::4", "Vector::i32::4", "i32"},
        {">", "<vec i32 4>", "f64", "Vector::Bool::4", "Vector::Float::4", "Float"},
        {"==", "<vec bool 4>", "bool", "Vector::Bool::4", "Vector::Bool::4", "Bool"},
        {"&", "<vec bool 4>", "<vec bool 4>", "Vector::Bool::4", "Vector::Bool::4", "Vector::Bool::4"},
        {"|", "bool", "<vec bool 4>", "Vector::Bool::4", "Bool", "Vector::Bool::4"},
        {"&&", "<vec i32 4>", "i32", "Vector::Bool::4", "Vector::Bool::4", "Bool"},
        {">>", "<vec u32 4>", "i64", "Vector::u32::4", "Vector::u32::4", "i64"},
        {"+", "i32", "f64", "Float", "Float", "Float"},
        {"/", "i32", "i32", "Float", "Float", "Float"},
        {"//", "i32", "i32", "i32", "i32", "i32"},
        {">>", "u32", "i64", "u32", "u32", "i64"},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.Left + " " + test.Op + " " + test.Right);
        auto result = AnnotateBinary(test.Op, test.Left, test.Right);
        ASSERT_TRUE(result.has_value()) << result.error().ToString();
        auto binary = TMaybeNode<TBinaryExpr>(*result).Cast();
        ASSERT_NE(binary, nullptr);
        EXPECT_EQ(TypeName(binary->Type), test.Result);
        EXPECT_EQ(TypeName(binary->Left->Type), test.LeftOperand);
        EXPECT_EQ(TypeName(binary->Right->Type), test.RightOperand);
    }
}

TEST(BinaryTypeAnnotation, RejectsInvalidVectorOperands) {
    struct TCase {
        std::string Op;
        std::string Left;
        std::string Right;
    };
    const std::vector<TCase> cases = {
        {"+", "<vec i32 4>", "<vec i32 8>"},
        {"+", "<vec i32 0>", "i32"},
        {"+", "i32", "<vec i32 0>"},
        {"+", "<vec i32 4>", "string"},
        {"+", "<vec i32 4>", "bool"},
        {"+", "<vec bool 4>", "<vec bool 4>"},
        {"*", "<vec bool 4>", "bool"},
        {"+", "<vec i64 4>", "<vec u64 4>"},
        {"//", "<vec f64 4>", "<vec f64 4>"},
        {"%", "<vec f64 4>", "i32"},
        {"&", "<vec f64 4>", "i32"},
        {"<<", "<vec bool 4>", "bool"},
    };
    for (const auto& test : cases) {
        SCOPED_TRACE(test.Left + " " + test.Op + " " + test.Right);
        auto result = AnnotateBinary(test.Op, test.Left, test.Right);
        ASSERT_FALSE(result.has_value());
        EXPECT_NE(result.error().ToString().find("Line: 1"), std::string::npos);
    }
}

TEST(BinaryTypeAnnotation, StringAndSymbolOperators) {
    for (const auto& left : {"string", "char"}) {
        for (const auto& right : {"string", "char"}) {
            for (const auto& op : {"+", "==", "!=", "<", "<=", ">", ">="}) {
                SCOPED_TRACE(std::string(left) + " " + op + " " + right);
                auto result = AnnotateBinary(op, left, right);
                ASSERT_TRUE(result.has_value()) << result.error().ToString();
                auto binary = TMaybeNode<TBinaryExpr>(*result).Cast();
                ASSERT_NE(binary, nullptr);
                EXPECT_EQ(TypeName(binary->Type), std::string(op) == "+" ? "String" : "Bool");
                if (std::string(op) == "+" || std::string(left) == "string" || std::string(right) == "string") {
                    EXPECT_EQ(TypeName(binary->Left->Type), "String");
                    EXPECT_EQ(TypeName(binary->Right->Type), "String");
                }
            }
        }
    }
    EXPECT_FALSE(AnnotateBinary("+", "string", "i32").has_value());
    EXPECT_FALSE(AnnotateBinary("*", "string", "string").has_value());
}

TEST(BinaryTypeAnnotation, ScalarLiteralUsesVectorElementType) {
    auto root = Annotate("(block (var a <vec i8 4>) (var result = (+ a 1)))");
    ASSERT_TRUE(root.has_value()) << root.error().ToString();
    auto value = TMaybeNode<TVarStmt>(TMaybeNode<TBlockExpr>(*root).Cast()->Stmts.back()).Cast()->Init;
    auto binary = TMaybeNode<TBinaryExpr>(value).Cast();
    ASSERT_NE(binary, nullptr);
    EXPECT_EQ(TypeName(binary->Type), "Vector::i8::4");
    EXPECT_EQ(TypeName(binary->Right->Type), "i8");
}

TEST(BinaryTypeAnnotation, VectorAssignmentsCheckShapeAndElementType) {
    EXPECT_FALSE(Annotate("(block (var a <vec i32 4>) (var b <vec i32 8>) (= b a))").has_value());
    EXPECT_FALSE(Annotate("(block (var a <vec i64 4>) (var b <vec i32 4>) (= b a))").has_value());
    auto root = Annotate("(block (var a <vec i32 4>) (var b <vec f64 4>) (= b a))");
    ASSERT_TRUE(root.has_value()) << root.error().ToString();
    auto assignment = TMaybeNode<TAssignExpr>(TMaybeNode<TBlockExpr>(*root).Cast()->Stmts.back()).Cast();
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(TypeName(assignment->Value->Type), "Vector::Float::4");
    EXPECT_TRUE(TMaybeNode<TCastExpr>(assignment->Value));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
