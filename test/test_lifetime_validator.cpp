#include <gtest/gtest.h>

#include <qumir/semantics/lifetime/validator.h>
#include <qumir/semantics/transform/transform.h>

#include <memory>
#include <string>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NSemantics;

namespace {

std::shared_ptr<TBlockExpr> Block(std::vector<TExprPtr> statements) {
    return std::make_shared<TBlockExpr>(TLocation{}, std::move(statements));
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
        std::vector<TGenericParam>{},
        std::move(parameters),
        std::move(body),
        std::move(returnType));
}

std::shared_ptr<TVarStmt> Variable(std::string name, TTypePtr type) {
    return std::make_shared<TVarStmt>(TLocation{}, std::move(name), std::move(type));
}

std::shared_ptr<TIdentExpr> Ident(std::string name) {
    return std::make_shared<TIdentExpr>(TLocation{}, std::move(name));
}

std::shared_ptr<TOwnLiteralExpr> OwnedLiteral(std::string value = "value") {
    return std::make_shared<TOwnLiteralExpr>(
        TLocation{},
        std::make_shared<TStringLiteralExpr>(TLocation{}, std::move(value)));
}

std::expected<void, TError> Prepare(
    TExprPtr& root,
    TNameResolver& resolver)
{
    if (auto error = resolver.Resolve(root)) {
        return std::unexpected(*error);
    }
    auto annotation = NTransform::FinalTypeAnnotation(root, resolver);
    if (!annotation) {
        return std::unexpected(annotation.error());
    }
    return {};
}

std::expected<void, TError> PrepareAndValidate(
    TExprPtr& root,
    TNameResolver& resolver)
{
    if (auto result = Prepare(root, resolver); !result) {
        return result;
    }
    return TLifetimeValidator(resolver).Validate(root);
}

void ExpectErrorContains(
    const std::expected<void, TError>& result,
    std::string_view expected)
{
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().ToString().find(expected), std::string::npos)
        << result.error().ToString();
}

} // namespace

TEST(LifetimeAnnotation, AnnotatesAllInternalNodesExplicitly) {
    auto stringType = std::make_shared<TStringType>();
    auto value = Variable("value", stringType);
    auto borrowed = std::make_shared<TBorrowExpr>(TLocation{}, Ident("value"));
    auto retained = std::make_shared<TRetainExpr>(TLocation{}, borrowed);
    auto moved = std::make_shared<TMoveExpr>(TLocation{}, OwnedLiteral("moved"));
    auto replace = std::make_shared<TReplaceExpr>(
        TLocation{},
        Ident("value"),
        OwnedLiteral("replacement"));
    auto destroyRetained = std::make_shared<TDestroyExpr>(TLocation{}, retained);
    auto destroyMoved = std::make_shared<TDestroyExpr>(TLocation{}, moved);
    auto exit = std::make_shared<TCleanupExitExpr>(
        TLocation{},
        ECleanupExitKind::Return,
        nullptr,
        std::vector<TExprPtr>{
            std::make_shared<TDestroyExpr>(TLocation{}, Ident("value")),
        });
    auto global = std::make_shared<TGlobalCleanupExpr>(
        TLocation{},
        std::vector<TExprPtr>{});
    TExprPtr root = Block({
        Function("main", {}, Block({
            value,
            destroyRetained,
            destroyMoved,
            replace,
            exit,
        })),
        global,
    });
    TNameResolver resolver;

    auto result = Prepare(root, resolver);
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_TRUE(TMaybeType<TStringType>(borrowed->Type));
    EXPECT_TRUE(TMaybeType<TStringType>(retained->Type));
    EXPECT_TRUE(TMaybeType<TStringType>(moved->Type));
    EXPECT_TRUE(TMaybeType<TVoidType>(destroyRetained->Type));
    EXPECT_TRUE(TMaybeType<TVoidType>(replace->Type));
    EXPECT_TRUE(TMaybeType<TVoidType>(exit->Type));
    EXPECT_TRUE(TMaybeType<TVoidType>(global->Type));
}

TEST(LifetimeAnnotation, CleanupReturnChecksFunctionReturnType) {
    auto exit = std::make_shared<TCleanupExitExpr>(
        TLocation{},
        ECleanupExitKind::Return,
        std::make_shared<TStringLiteralExpr>(TLocation{}, "wrong"),
        std::vector<TExprPtr>{});
    TExprPtr root = Block({
        Function(
            "value",
            {},
            Block({exit}),
            std::make_shared<TIntegerType>()),
    });
    TNameResolver resolver;

    auto result = Prepare(root, resolver);
    ExpectErrorContains(result, "возвращаемому типу функции");
}

TEST(LifetimeValidator, AcceptsValidOwnershipGraph) {
    auto value = Variable("value", std::make_shared<TStringType>());
    value->Init = OwnedLiteral("initial");
    auto replace = std::make_shared<TReplaceExpr>(
        TLocation{},
        Ident("value"),
        std::make_shared<TRetainExpr>(
            TLocation{},
            std::make_shared<TBorrowExpr>(TLocation{}, Ident("value"))));
    auto destroy = std::make_shared<TDestroyExpr>(TLocation{}, Ident("value"));
    TExprPtr root = Block({Function("main", {}, Block({value, replace, destroy}))});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
}

TEST(LifetimeValidator, RejectsRetainOfTrivialValue) {
    auto retain = std::make_shared<TRetainExpr>(
        TLocation{},
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}));
    TExprPtr root = Block({retain});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "ref-counted");
}

TEST(LifetimeValidator, RejectsRetainOfUniqueValue) {
    auto arrayType = std::make_shared<TArrayType>(std::make_shared<TIntegerType>(), 1);
    auto array = Variable("items", arrayType);
    auto retain = std::make_shared<TRetainExpr>(
        TLocation{},
        std::make_shared<TBorrowExpr>(TLocation{}, Ident("items")));
    auto destroy = std::make_shared<TDestroyExpr>(TLocation{}, retain);
    TExprPtr root = Block({Function("main", {}, Block({array, destroy}))});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "ref-counted");
}

TEST(LifetimeValidator, RejectsDestroyOfTrivialValue) {
    auto destroy = std::make_shared<TDestroyExpr>(
        TLocation{},
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}));
    TExprPtr root = Block({destroy});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "managed value");
}

TEST(LifetimeValidator, RejectsDestroyOfBorrowedParameter) {
    auto parameter = Variable("value", std::make_shared<TStringType>());
    auto destroy = std::make_shared<TDestroyExpr>(TLocation{}, Ident("value"));
    TExprPtr root = Block({Function("main", {parameter}, Block({destroy}))});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "borrowed value");
}

TEST(LifetimeValidator, RejectsCopyOfUniqueValue) {
    auto arrayType = std::make_shared<TArrayType>(std::make_shared<TIntegerType>(), 1);
    auto source = Variable("source", arrayType);
    auto target = Variable("target", arrayType);
    auto replace = std::make_shared<TReplaceExpr>(
        TLocation{},
        Ident("target"),
        Ident("source"));
    TExprPtr root = Block({Function("main", {}, Block({source, target, replace}))});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "Unique value cannot be copied");
}

TEST(LifetimeValidator, RejectsUnresolvedSyntheticVariable) {
    auto ident = Ident("__lifetime_missing");
    ident->Type = std::make_shared<TStringType>();
    TExprPtr root = Block({ident});
    TNameResolver resolver;

    auto result = TLifetimeValidator(resolver).Validate(root);
    ExpectErrorContains(result, "Unresolved synthetic lifetime variable");
}

TEST(LifetimeValidator, RejectsRawLiteralInOwnedStorage) {
    auto value = Variable("value", std::make_shared<TStringType>());
    value->Init = std::make_shared<TStringLiteralExpr>(TLocation{}, "raw");
    auto marker = std::make_shared<TGlobalCleanupExpr>(
        TLocation{},
        std::vector<TExprPtr>{});
    TExprPtr root = Block({value, marker});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "Raw string literal requires own-literal");
}

TEST(LifetimeValidator, RejectsUnconsumedOwnedResult) {
    TExprPtr root = Block({OwnedLiteral("unused")});
    TNameResolver resolver;

    auto result = PrepareAndValidate(root, resolver);
    ExpectErrorContains(result, "Owned result has no move/destroy consumer");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
