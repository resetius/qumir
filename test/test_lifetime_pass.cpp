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

std::shared_ptr<TCallExpr> Call(
    std::string name,
    std::vector<TExprPtr> arguments = {})
{
    return std::make_shared<TCallExpr>(
        TLocation{},
        Ident(std::move(name)),
        std::move(arguments));
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

TEST(LifetimePass, DestroysTwoOwnedCallArgumentsInReverseOrder) {
    auto stringType = std::make_shared<TStringType>();
    auto makeFirst = Function(
        "make_first",
        {},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Literal("first"))}),
        stringType);
    auto makeSecond = Function(
        "make_second",
        {},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Literal("second"))}),
        stringType);
    auto consume = Function(
        "consume",
        {Variable("first", stringType), Variable("second", stringType)},
        Block({std::make_shared<TReturnExpr>(TLocation{}, nullptr)}));
    auto body = Block({Call("consume", {
        Call("make_first"),
        Call("make_second"),
    })});
    TExprPtr root = Block({
        makeFirst,
        makeSecond,
        consume,
        Function("main", {}, body),
    });
    NSemantics::TNameResolver resolver;

    auto source = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(source.has_value()) << source.error().ToString();
    auto final = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(final.has_value()) << final.error().ToString();

    ASSERT_EQ(body->Stmts.size(), 1u);
    EXPECT_EQ(
        Print(body->Stmts[0]),
        "(block (var __lifetime_0 = (move (call make_first))) "
        "(var __lifetime_1 = (move (call make_second))) "
        "(call consume (borrow __lifetime_0) (borrow __lifetime_1)) "
        "(destroy __lifetime_1) (destroy __lifetime_0))");
}

TEST(LifetimePass, PreservesNestedCallResultPastArgumentCleanup) {
    auto stringType = std::make_shared<TStringType>();
    auto make = Function(
        "make",
        {},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Literal("value"))}),
        stringType);
    auto wrap = Function(
        "wrap",
        {Variable("value", stringType)},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Ident("value"))}),
        stringType);
    auto result = Variable("result", stringType);
    auto body = Block({
        result,
        std::make_shared<TAssignExpr>(
            TLocation{},
            "result",
            Call("wrap", {Call("make")})),
    });
    TExprPtr root = Block({make, wrap, Function("main", {}, body)});
    NSemantics::TNameResolver resolver;

    auto source = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(source.has_value()) << source.error().ToString();
    auto final = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(final.has_value()) << final.error().ToString();

    ASSERT_EQ(body->Stmts.size(), 2u);
    EXPECT_EQ(
        Print(body->Stmts[1]),
        "(replace result (move (block "
        "(var __lifetime_0 = (move (call make))) "
        "(var __lifetime_1 = (call wrap (borrow __lifetime_0))) "
        "(destroy __lifetime_0) (move __lifetime_1))))");
}

TEST(LifetimePass, MaterializesLiteralOnlyWhenCallAbiRequiresIt) {
    auto stringType = std::make_shared<TStringType>();
    auto raw = Function(
        "raw",
        {Variable("value", stringType)},
        Block({}),
        std::make_shared<TVoidType>());
    raw->MangledName = "raw";
    auto managed = Function(
        "managed",
        {Variable("value", stringType)},
        Block({}),
        std::make_shared<TVoidType>());
    managed->MangledName = "managed";
    managed->RequireArgsMaterialization = true;
    auto body = Block({
        Call("raw", {Literal("raw")}),
        Call("managed", {Literal("managed")}),
        Call("make_unused"),
    });
    auto makeUnused = Function(
        "make_unused",
        {},
        Block({std::make_shared<TReturnExpr>(TLocation{}, Literal("unused"))}),
        stringType);
    TExprPtr root = Block({
        raw,
        managed,
        makeUnused,
        Function("main", {}, body),
    });
    NSemantics::TNameResolver resolver;

    auto source = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(source.has_value()) << source.error().ToString();
    auto final = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(final.has_value()) << final.error().ToString();

    ASSERT_EQ(body->Stmts.size(), 3u);
    EXPECT_EQ(Print(body->Stmts[0]), "(call raw \"raw\")");
    EXPECT_EQ(
        Print(body->Stmts[1]),
        "(block (var __lifetime_0 = (own-literal \"managed\")) "
        "(call managed (borrow __lifetime_0)) (destroy __lifetime_0))");
    EXPECT_EQ(
        Print(body->Stmts[2]),
        "(destroy (move (call make_unused)))");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
