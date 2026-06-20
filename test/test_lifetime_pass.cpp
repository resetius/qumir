#include <gtest/gtest.h>

#include <qumir/parser/core/printer.h>
#include <qumir/semantics/transform/transform.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;

namespace {

std::shared_ptr<TBlockExpr> Block(std::vector<TExprPtr> statements) {
    return std::make_shared<TBlockExpr>(TLocation{}, std::move(statements));
}

std::shared_ptr<TVarStmt> Variable(std::string name, TTypePtr type) {
    return std::make_shared<TVarStmt>(TLocation{}, std::move(name), std::move(type));
}

std::shared_ptr<TIdentExpr> Ident(std::string name) {
    return std::make_shared<TIdentExpr>(TLocation{}, std::move(name));
}

std::shared_ptr<TStringLiteralExpr> Literal(std::string value) {
    return std::make_shared<TStringLiteralExpr>(TLocation{}, std::move(value));
}

std::shared_ptr<TFunDecl> Function(
    std::string name,
    std::vector<TParam> parameters,
    std::shared_ptr<TBlockExpr> body,
    TTypePtr returnType = std::make_shared<TVoidType>())
{
    return std::make_shared<TFunDecl>(
        TLocation{},
        std::move(name),
        std::move(parameters),
        std::move(body),
        std::move(returnType));
}

std::string Print(const TExprPtr& expr) {
    NAst::NCore::TPrintOptions options;
    options.Pretty = false;
    return NAst::NCore::PrintAst(expr, options);
}

} // namespace

TEST(LifetimePass, RewritesAllStringAssignmentTargets) {
    auto stringType = std::make_shared<TStringType>();
    auto make = Function(
        "make",
        {},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Literal("owned"))}),
        stringType);

    auto a = Variable("a", stringType);
    auto b = Variable("b", stringType);
    auto arrayType = std::make_shared<TArrayType>(stringType, 1);
    auto items = Variable("items", arrayType);
    auto structType = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"text", stringType},
        });
    auto object = Variable("object", structType);
    auto body = Block({
        a,
        b,
        std::make_shared<TAssignExpr>(TLocation{}, "a", Literal("literal")),
        std::make_shared<TAssignExpr>(TLocation{}, "a", Ident("b")),
        std::make_shared<TAssignExpr>(TLocation{}, "a", Ident("a")),
        std::make_shared<TAssignExpr>(
            TLocation{},
            "a",
            std::make_shared<TCallExpr>(TLocation{}, Ident("make"), std::vector<TExprPtr>{})),
        items,
        std::make_shared<TArrayAssignExpr>(
            TLocation{},
            "items",
            std::vector<TExprPtr>{
                std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}),
            },
            Literal("element")),
        object,
        std::make_shared<TFieldAssignExpr>(
            TLocation{},
            Ident("object"),
            "text",
            Ident("b")),
    });
    auto rewrite = Function("rewrite", {}, body);

    auto refParam = Variable(
        "result",
        std::make_shared<TReferenceType>(stringType));
    auto refBody = Block({
        std::make_shared<TAssignExpr>(TLocation{}, "result", Literal("reference")),
    });
    auto writeRef = Function("write_ref", {refParam}, refBody);

    TExprPtr root = Block({make, rewrite, writeRef});
    NSemantics::TNameResolver resolver;
    auto source = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(source.has_value()) << source.error().ToString();
    auto final = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(final.has_value()) << final.error().ToString();

    ASSERT_EQ(body->Stmts.size(), 10u);
    EXPECT_EQ(Print(a->Init), "(bitcast 0 string)");
    EXPECT_EQ(Print(b->Init), "(bitcast 0 string)");
    EXPECT_EQ(Print(body->Stmts[2]), "(replace a (own-literal \"literal\"))");
    EXPECT_EQ(Print(body->Stmts[3]), "(replace a (retain (borrow b)))");
    EXPECT_EQ(Print(body->Stmts[4]), "(replace a (retain (borrow a)))");
    EXPECT_EQ(Print(body->Stmts[5]), "(replace a (move (call make)))");
    EXPECT_EQ(
        Print(body->Stmts[7]),
        "(replace (index items 1) (own-literal \"element\"))");
    EXPECT_EQ(
        Print(body->Stmts[9]),
        "(replace (field object text) (retain (borrow b)))");
    EXPECT_EQ(
        Print(refBody->Stmts[0]),
        "(replace result (own-literal \"reference\"))");
}

TEST(LifetimePass, SplitsStringDeclarationInitializerAfterNullInitialization) {
    auto value = Variable("value", std::make_shared<TStringType>());
    value->Init = Literal("initial");
    auto body = Block({value});
    TExprPtr root = Block({Function("main", {}, body)});
    NSemantics::TNameResolver resolver;

    auto source = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(source.has_value()) << source.error().ToString();
    auto final = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(final.has_value()) << final.error().ToString();

    ASSERT_EQ(body->Stmts.size(), 2u);
    EXPECT_EQ(Print(value->Init), "(bitcast 0 string)");
    EXPECT_EQ(
        Print(body->Stmts[1]),
        "(replace value (own-literal \"initial\"))");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
