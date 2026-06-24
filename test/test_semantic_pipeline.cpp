#include <gtest/gtest.h>

#include <qumir/modules/module.h>
#include <qumir/modules/system/system.h>
#include <qumir/parser/core/printer.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/kumir/pipeline.h>
#include <qumir/semantics/lifetime/pass.h>
#include <qumir/semantics/lifetime/synthetic_name_generator.h>
#include <qumir/semantics/return_normalization/pass.h>
#include <qumir/semantics/transform/transform.h>

#include <memory>
#include <sstream>
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

std::string Print(const TExprPtr& expr) {
    NAst::NCore::TPrintOptions options;
    options.Pretty = false;
    return NAst::NCore::PrintAst(expr, options);
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

TEST(SourceTransformExtensions, RunInStablePhaseOrder) {
    TExprPtr root = MakeRoot({});
    TNameResolver resolver;
    std::vector<std::string> phases;
    NTransform::TPipelineOptions options;
    options.Extensions.BeforeNameResolution.push_back(
        [&](TExprPtr&, TNameResolver&) -> std::expected<bool, TError> {
            phases.push_back("before-name");
            return false;
        });
    options.Extensions.AfterNameResolution.push_back(
        [&](TExprPtr&, TNameResolver&) -> std::expected<bool, TError> {
            phases.push_back("after-name");
            return false;
        });
    options.Extensions.AfterTypeAnnotation.push_back(
        [&](TExprPtr&, TNameResolver&) -> std::expected<bool, TError> {
            phases.push_back("after-type");
            return false;
        });

    auto result = NTransform::RunSourceTransformFixpoint(
        root,
        resolver,
        std::move(options));

    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_EQ(
        phases,
        (std::vector<std::string>{
            "before-name",
            "after-name",
            "after-type",
            "before-name",
            "after-name",
        }));
}

TEST(SourceTransformExtensions, PropagateErrorsAndStopLaterPasses) {
    TExprPtr root = MakeRoot({});
    TNameResolver resolver;
    bool laterPassRan = false;
    NTransform::TPipelineOptions options;
    options.Extensions.BeforeNameResolution.push_back(
        [](TExprPtr&, TNameResolver&) -> std::expected<bool, TError> {
            return std::unexpected(TError("injected failure"));
        });
    options.Extensions.AfterNameResolution.push_back(
        [&](TExprPtr&, TNameResolver&) -> std::expected<bool, TError> {
            laterPassRan = true;
            return false;
        });

    auto result = NTransform::RunSourceTransformFixpoint(
        root,
        resolver,
        std::move(options));

    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().ToString().find("injected failure"), std::string::npos);
    EXPECT_FALSE(laterPassRan);
}

TEST(KumirPipeline, InjectsCoroutineAnnotationAfterTypeAnnotation) {
    auto extensions = NSemantics::NKumir::PipelineExtensions();

    EXPECT_TRUE(extensions.BeforeNameResolution.empty());
    EXPECT_TRUE(extensions.AfterNameResolution.empty());
    EXPECT_EQ(extensions.AfterTypeAnnotation.size(), 2u);
}

TEST(KumirPipeline, ParserEmitsDistinctPowerOperator) {
    NRegistry::SystemModule system;
    TNameResolver resolver;
    resolver.RegisterModule(&system);
    ASSERT_TRUE(resolver.ImportModule(system.Name()).has_value());
    std::istringstream input(
        "алг\n"
        "нач\n"
        "  цел x\n"
        "  x := 2 ** 3\n"
        "кон\n");
    TTokenStream tokens(input);
    TParser parser;
    auto parsed = parser.parse(tokens, &resolver);
    ASSERT_TRUE(parsed.has_value()) << parsed.error().ToString();
    auto root = TMaybeNode<TBlockExpr>(*parsed).Cast();
    ASSERT_NE(root, nullptr);
    auto function = TMaybeNode<TFunDecl>(root->Stmts.front()).Cast();
    ASSERT_NE(function, nullptr);
    auto assignment = TMaybeNode<TAssignExpr>(function->Body->Stmts[1]).Cast();
    ASSERT_NE(assignment, nullptr);
    auto power = TMaybeNode<TBinaryExpr>(assignment->Value).Cast();
    ASSERT_NE(power, nullptr);
    EXPECT_EQ(power->Operator.ToString(), "**");
}

TEST(KumirParser, ParameterAccessModesAreExplicit) {
    NRegistry::SystemModule system;
    TNameResolver resolver;
    resolver.RegisterModule(&system);
    ASSERT_TRUE(resolver.ImportModule(system.Name()).has_value());
    std::istringstream input(
        "алг test(арг цел a, рез цел b, аргрез цел c)\n"
        "нач\n"
        "кон\n");
    TTokenStream tokens(input);
    TParser parser;

    auto parsed = parser.parse(tokens, &resolver);

    ASSERT_TRUE(parsed.has_value()) << parsed.error().ToString();
    auto root = TMaybeNode<TBlockExpr>(*parsed).Cast();
    ASSERT_NE(root, nullptr);
    auto function = TMaybeNode<TFunDecl>(root->Stmts.front()).Cast();
    ASSERT_NE(function, nullptr);
    ASSERT_EQ(function->Params.size(), 3u);
    auto inputType = UnwrapReferenceType(function->Params[0]->Type);
    auto outputType = UnwrapReferenceType(function->Params[1]->Type);
    auto inputOutputType = UnwrapReferenceType(function->Params[2]->Type);
    EXPECT_TRUE(inputType->Readable);
    EXPECT_FALSE(inputType->Mutable);
    EXPECT_FALSE(outputType->Readable);
    EXPECT_TRUE(outputType->Mutable);
    EXPECT_TRUE(inputOutputType->Readable);
    EXPECT_TRUE(inputOutputType->Mutable);
}

TEST(ModuleAliases, ResolveImportAndListThroughAlias) {
    NRegistry::SystemModule system;
    TNameResolver resolver;
    resolver.RegisterModule(&system);

    // Without an alias the legacy Kumir name is unknown.
    EXPECT_FALSE(resolver.ImportModule("Файлы").has_value());

    resolver.RegisterModuleAlias("Файлы", system.Name());
    EXPECT_TRUE(resolver.ImportModule("Файлы").has_value());
    EXPECT_NE(resolver.ModulesList().find("Файлы"), std::string::npos);
}

TEST(ModuleAliases, RejectsAliasToUnregisteredModule) {
    TNameResolver resolver;
    EXPECT_THROW(resolver.RegisterModuleAlias("Файлы", "System"), std::runtime_error);
}

TEST(ModuleAliases, KumirFrontendAliasesAreSystem) {
    auto aliases = NKumir::ModuleAliases();
    EXPECT_EQ(aliases.at("Файлы"), "System");
    EXPECT_EQ(aliases.at("Строки"), "System");
}

TEST(KumirPipeline, ExpandsPowerWithoutReusingCoreOperator) {
    NRegistry::SystemModule system;
    TNameResolver resolver;
    resolver.RegisterModule(&system);
    ASSERT_TRUE(resolver.ImportModule(system.Name()).has_value());
    auto right = std::make_shared<TNumberExpr>(TLocation{}, int64_t{3});
    right->Type = std::make_shared<TIntegerType>();
    TExprPtr ast = std::make_shared<TBinaryExpr>(
        TLocation{},
        TOperator("**"),
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{2}),
        std::move(right));

    auto result = NSemantics::NKumir::PowerTransform(ast, resolver);

    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_TRUE(*result);
    auto call = TMaybeNode<TCallExpr>(ast).Cast();
    ASSERT_NE(call, nullptr);
    auto callee = TMaybeNode<TIdentExpr>(call->Callee).Cast();
    ASSERT_NE(callee, nullptr);
    EXPECT_EQ(callee->Name, "fpow");
    EXPECT_EQ(call->Args.size(), 2u);
}

TEST(KumirPipeline, ExpandsFloatPowerToPow) {
    NRegistry::SystemModule system;
    TNameResolver resolver;
    resolver.RegisterModule(&system);
    ASSERT_TRUE(resolver.ImportModule(system.Name()).has_value());
    TExprPtr ast = std::make_shared<TBinaryExpr>(
        TLocation{},
        TOperator("**"),
        std::make_shared<TNumberExpr>(TLocation{}, 2.0),
        std::make_shared<TNumberExpr>(TLocation{}, 0.5));

    auto result = NTransform::RunSourceTransformFixpoint(
        ast,
        resolver,
        {.Extensions = NSemantics::NKumir::PipelineExtensions()});

    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    auto call = TMaybeNode<TCallExpr>(ast).Cast();
    ASSERT_NE(call, nullptr);
    auto callee = TMaybeNode<TIdentExpr>(call->Callee).Cast();
    ASSERT_NE(callee, nullptr);
    EXPECT_EQ(callee->Name, "pow");
}

TEST(ReturnNormalization, MakesFallthroughReturnsExplicit) {
    auto implicitValue = std::make_shared<TNumberExpr>(TLocation{}, int64_t{42});
    auto valueBody = MakeRoot({implicitValue});
    auto valueFunction = std::make_shared<TFunDecl>(
        TLocation{},
        "value",
        std::vector<TParam>{},
        valueBody,
        std::make_shared<TIntegerType>());
    auto voidBody = MakeRoot({});
    auto voidFunction = MakeVoidFunction("action", voidBody);
    TExprPtr root = MakeRoot({valueFunction, voidFunction});
    TNameResolver resolver;

    auto result = ReturnNormalizationPass(root, resolver);
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_TRUE(result.value());
    EXPECT_EQ(Print(valueBody), "(block (return 42))");
    EXPECT_EQ(Print(voidBody), "(block (return))");
}

TEST(ReturnNormalization, PreservesDirectAndFullyTerminatingBranchReturns) {
    auto directReturn = std::make_shared<TReturnExpr>(
        TLocation{},
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}));
    auto directBody = MakeRoot({directReturn});
    auto directFunction = std::make_shared<TFunDecl>(
        TLocation{},
        "direct",
        std::vector<TParam>{},
        directBody,
        std::make_shared<TIntegerType>());
    auto assertCondition = std::make_shared<TNumberExpr>(TLocation{}, int64_t{1});
    assertCondition->Type = std::make_shared<TBoolType>();
    directFunction->LastAssert = std::make_shared<TAssertStmt>(
        TLocation{},
        assertCondition);

    auto condition = std::make_shared<TNumberExpr>(TLocation{}, int64_t{1});
    condition->Type = std::make_shared<TBoolType>();
    auto elseIfCondition = std::make_shared<TNumberExpr>(TLocation{}, int64_t{0});
    elseIfCondition->Type = std::make_shared<TBoolType>();
    auto elseIf = std::make_shared<TIfExpr>(
        TLocation{},
        elseIfCondition,
        MakeRoot({std::make_shared<TReturnExpr>(
            TLocation{},
            std::make_shared<TNumberExpr>(TLocation{}, int64_t{3}))}),
        MakeRoot({std::make_shared<TReturnExpr>(
            TLocation{},
            std::make_shared<TNumberExpr>(TLocation{}, int64_t{4}))}));
    auto branch = std::make_shared<TIfExpr>(
        TLocation{},
        condition,
        MakeRoot({std::make_shared<TReturnExpr>(
            TLocation{},
            std::make_shared<TNumberExpr>(TLocation{}, int64_t{2}))}),
        elseIf);
    auto branchBody = MakeRoot({MakeRoot({MakeRoot({branch})})});
    auto branchFunction = std::make_shared<TFunDecl>(
        TLocation{},
        "branch",
        std::vector<TParam>{},
        branchBody,
        std::make_shared<TIntegerType>());
    TExprPtr root = MakeRoot({directFunction, branchFunction});
    TNameResolver resolver;

    auto result = ReturnNormalizationPass(root, resolver);
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_FALSE(result.value());
    EXPECT_EQ(directBody->Stmts.size(), 1u);
    EXPECT_EQ(branchBody->Stmts.size(), 1u);
    EXPECT_EQ(
        Print(directFunction),
        "(fun direct () -> i64 (attrs (expect_after (assert #t))) "
        "(block (return 1)))");
    EXPECT_EQ(
        Print(branchBody),
        "(block (block (block (if #t (block (return 2)) "
        "(if #f (block (return 3)) (block (return 4)))))))");
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
