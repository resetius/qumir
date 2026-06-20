#include <gtest/gtest.h>

#include <qumir/modules/module.h>
#include <qumir/semantics/lifetime/pass.h>
#include <qumir/semantics/lifetime/synthetic_name_generator.h>
#include <qumir/semantics/return_normalization/pass.h>
#include <qumir/semantics/transform/transform.h>

#include <memory>
#include <string>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NRegistry;
using namespace NQumir::NSemantics;

namespace {

class TCollisionModule final : public IModule {
public:
    TCollisionModule()
        : Functions({TExternalFunction {
            .Name = "__lifetime_0",
            .MangledName = "collision_lifetime_0",
            .ReturnType = std::make_shared<TVoidType>(),
        }})
        , Types({TExternalType {
            .Name = "__lifetime_1",
            .Type = std::make_shared<TIntegerType>(),
        }})
    { }

    const std::string& Name() const override { return ModuleName; }
    const std::vector<TExternalFunction>& ExternalFunctions() const override { return Functions; }
    const std::vector<TExternalType>& ExternalTypes() const override { return Types; }
    const std::vector<TLiteralSuffix>& LiteralSuffixes() const override { return Suffixes; }
    const std::vector<std::string>& Dependencies() const override { return ModuleDependencies; }

private:
    std::string ModuleName = "SyntheticCollision";
    std::vector<TExternalFunction> Functions;
    std::vector<TExternalType> Types;
    std::vector<TLiteralSuffix> Suffixes;
    std::vector<std::string> ModuleDependencies;
};

std::shared_ptr<TBlockExpr> MakeRoot(std::vector<TExprPtr> statements) {
    return std::make_shared<TBlockExpr>(TLocation{}, std::move(statements));
}

std::shared_ptr<TFunDecl> MakeVoidFunction(
    std::string name,
    const std::shared_ptr<TBlockExpr>& body)
{
    return std::make_shared<TFunDecl>(
        TLocation{},
        std::move(name),
        std::vector<TParam>{},
        body,
        std::make_shared<TVoidType>());
}

} // namespace

TEST(SyntheticNameGenerator, AvoidsSourceAndImportedNamesPerPass) {
    TCollisionModule module;
    TNameResolver resolver;
    resolver.RegisterModule(&module);
    auto importResult = resolver.ImportModule(module.Name());
    ASSERT_TRUE(importResult.has_value()) << importResult.error();

    auto sourceVar = std::make_shared<TVarStmt>(
        TLocation{},
        "__lifetime_2",
        std::make_shared<TIntegerType>());
    TExprPtr root = MakeRoot({sourceVar});
    ASSERT_FALSE(resolver.Resolve(root).has_value());

    TSyntheticNameGenerator firstPass(resolver, root);
    EXPECT_EQ(firstPass.Next(), "__lifetime_3");
    EXPECT_EQ(firstPass.Next(), "__lifetime_4");

    TSyntheticNameGenerator secondPass(resolver, root);
    EXPECT_EQ(secondPass.Next(), "__lifetime_3");
}

TEST(FinalSemanticPipeline, InitialPassesDoNotChangeAst) {
    TExprPtr root = MakeRoot({});
    const auto original = root;
    TNameResolver resolver;
    TSyntheticNameGenerator names(resolver, root);

    auto returnResult = ReturnNormalizationPass(root, resolver);
    ASSERT_TRUE(returnResult.has_value());
    EXPECT_FALSE(returnResult.value());

    auto lifetimeResult = LifetimePass(root, resolver, names);
    ASSERT_TRUE(lifetimeResult.has_value());
    EXPECT_FALSE(lifetimeResult.value());
    EXPECT_EQ(root, original);
}

TEST(FinalSemanticPipeline, ResolvesAndAnnotatesInsertedNestedBlock) {
    auto body = MakeRoot({});
    auto function = MakeVoidFunction("main", body);
    TExprPtr root = MakeRoot({function});
    TNameResolver resolver;

    auto sourceResult = NTransform::RunSourceTransformFixpoint(root, resolver);
    ASSERT_TRUE(sourceResult.has_value()) << sourceResult.error().ToString();

    TSyntheticNameGenerator names(resolver, root);
    const auto syntheticName = names.Next();
    auto syntheticVar = std::make_shared<TVarStmt>(
        TLocation{},
        syntheticName,
        std::make_shared<TIntegerType>());
    syntheticVar->Init = std::make_shared<TNumberExpr>(TLocation{}, int64_t{7});
    auto syntheticUse = std::make_shared<TIdentExpr>(TLocation{}, syntheticName);
    auto nested = MakeRoot({syntheticVar, syntheticUse});
    body->Stmts.push_back(nested);
    body->Stmts.push_back(std::make_shared<TReturnExpr>(TLocation{}, nullptr));

    ASSERT_EQ(nested->Scope, -1);
    auto nameResult = NTransform::FinalNameResolution(root, resolver);
    ASSERT_TRUE(nameResult.has_value()) << nameResult.error().ToString();
    ASSERT_GE(nested->Scope, 0);
    EXPECT_NE(nested->Scope, body->Scope);

    auto symbol = resolver.Lookup(syntheticName, TScopeId{nested->Scope});
    ASSERT_TRUE(symbol.has_value());
    EXPECT_EQ(symbol->DeclScopeId, nested->Scope);
    EXPECT_EQ(symbol->FuncScopeId, function->Scope);
    EXPECT_EQ(resolver.GetSymbolNode(TSymbolId{symbol->Id}), syntheticVar);

    auto typeResult = NTransform::FinalTypeAnnotation(root, resolver);
    ASSERT_TRUE(typeResult.has_value()) << typeResult.error().ToString();
    EXPECT_TRUE(TMaybeType<TIntegerType>(syntheticUse->Type));
}

TEST(FinalSemanticPipeline, DoesNotRerunSourceTransforms) {
    auto condition = std::make_shared<TNumberExpr>(TLocation{}, true);
    auto assertNode = std::make_shared<TAssertStmt>(TLocation{}, condition);
    assertNode->Type = std::make_shared<TVoidType>();
    auto body = MakeRoot({assertNode, std::make_shared<TReturnExpr>(TLocation{}, nullptr)});
    TExprPtr root = MakeRoot({MakeVoidFunction("main", body)});
    TNameResolver resolver;

    auto result = NTransform::RunFinalSemanticPipeline(root, resolver);
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    ASSERT_EQ(body->Stmts.size(), 2u);
    EXPECT_TRUE(TMaybeNode<TAssertStmt>(body->Stmts[0]));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
