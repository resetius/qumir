#include <gtest/gtest.h>

#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/modules/system/system.h>
#include <qumir/semantics/lifetime/pass.h>
#include <qumir/semantics/lifetime/synthetic_name_generator.h>
#include <qumir/semantics/transform/transform.h>

#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NIR;

namespace {

struct TLowered {
    std::string Trace;
    std::string Ir;
};

std::shared_ptr<TIdentExpr> Ident(std::string name) {
    return std::make_shared<TIdentExpr>(TLocation{}, std::move(name));
}

std::shared_ptr<TOwnLiteralExpr> OwnedLiteral(std::string value = "value") {
    return std::make_shared<TOwnLiteralExpr>(
        TLocation{},
        std::make_shared<TStringLiteralExpr>(TLocation{}, std::move(value)));
}

std::shared_ptr<TDestroyExpr> Destroy(TExprPtr value, TExprPtr aux = nullptr) {
    return std::make_shared<TDestroyExpr>(
        TLocation{},
        std::move(value),
        std::move(aux));
}

std::shared_ptr<TFunDecl> Function(
    std::vector<TParam> parameters,
    std::vector<TExprPtr> statements)
{
    return std::make_shared<TFunDecl>(
        TLocation{},
        "main",
        std::move(parameters),
        std::make_shared<TBlockExpr>(TLocation{}, std::move(statements)),
        std::make_shared<TVoidType>());
}

std::string InstructionName(
    const TInstr& instruction,
    const NSemantics::TNameResolver& resolver)
{
    auto name = instruction.Op.ToString();
    if (name != "call" || instruction.OperandCount != 1
        || instruction.Operands[0].Type != TOperand::EType::Imm)
    {
        return name;
    }
    auto callee = resolver.GetSymbolNode(
        NSemantics::TSymbolId{
            static_cast<int32_t>(instruction.Operands[0].Imm.Value),
        });
    auto function = TMaybeNode<TFunDecl>(callee);
    return function
        ? "call:" + function.Cast()->Name
        : "call:<unknown>";
}

std::expected<TLowered, TError> Lower(
    std::vector<TParam> parameters,
    std::vector<TExprPtr> statements)
{
    auto function = Function(std::move(parameters), std::move(statements));
    TExprPtr root = std::make_shared<TBlockExpr>(
        TLocation{},
        std::vector<TExprPtr>{function});
    NSemantics::TNameResolver resolver;
    NRegistry::SystemModule system;
    resolver.RegisterModule(&system);
    auto import = resolver.ImportModule(system.Name());
    if (!import) {
        return std::unexpected(TError({}, import.error()));
    }
    if (auto error = resolver.Resolve(root)) {
        return std::unexpected(*error);
    }
    if (auto annotation = NTransform::FinalTypeAnnotation(root, resolver); !annotation) {
        return std::unexpected(annotation.error());
    }
    NSemantics::TSyntheticNameGenerator syntheticNames(resolver, root);
    auto lifetime = NSemantics::LifetimePass(root, resolver, syntheticNames);
    if (!lifetime) {
        return std::unexpected(lifetime.error());
    }
    if (lifetime.value()) {
        if (auto resolution = NTransform::FinalNameResolution(root, resolver); !resolution) {
            return std::unexpected(resolution.error());
        }
        if (auto annotation = NTransform::FinalTypeAnnotation(root, resolver); !annotation) {
            return std::unexpected(annotation.error());
        }
    }

    TModule module;
    TBuilder builder(module);
    TAstLowerer lowerer(module, builder, resolver);
    if (auto result = lowerer.LowerTop(root); !result) {
        return std::unexpected(result.error());
    }

    std::string trace;
    auto* loweredFunction = module.GetFunctionByName("main");
    if (!loweredFunction) {
        return std::unexpected(TError({}, "lowered function not found"));
    }
    for (const auto& block : loweredFunction->Blocks) {
        for (const auto& instruction : block.Instrs) {
            if (!trace.empty()) {
                trace += " ";
            }
            trace += InstructionName(instruction, resolver);
        }
    }
    std::ostringstream ir;
    module.Print(ir);
    return TLowered{
        .Trace = std::move(trace),
        .Ir = ir.str(),
    };
}

std::expected<TLowered, TError> LowerNamedFunction(
    TExprPtr root,
    std::string_view functionName)
{
    NSemantics::TNameResolver resolver;
    NRegistry::SystemModule system;
    resolver.RegisterModule(&system);
    auto import = resolver.ImportModule(system.Name());
    if (!import) {
        return std::unexpected(TError({}, import.error()));
    }
    if (auto source = NTransform::RunSourceTransformFixpoint(root, resolver); !source) {
        return std::unexpected(source.error());
    }
    if (auto final = NTransform::RunFinalSemanticPipeline(root, resolver); !final) {
        return std::unexpected(final.error());
    }

    TModule module;
    TBuilder builder(module);
    TAstLowerer lowerer(module, builder, resolver);
    if (auto result = lowerer.LowerTop(root); !result) {
        return std::unexpected(result.error());
    }
    auto* function = module.GetFunctionByName(std::string(functionName));
    if (!function) {
        return std::unexpected(TError({}, "lowered function not found"));
    }

    std::string trace;
    for (const auto& block : function->Blocks) {
        for (const auto& instruction : block.Instrs) {
            if (!trace.empty()) {
                trace += " ";
            }
            trace += InstructionName(instruction, resolver);
        }
    }
    std::ostringstream ir;
    module.Print(ir);
    return TLowered{
        .Trace = std::move(trace),
        .Ir = ir.str(),
    };
}

void ExpectTrace(
    std::vector<TParam> parameters,
    std::vector<TExprPtr> statements,
    std::string_view expected)
{
    auto result = Lower(std::move(parameters), std::move(statements));
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_EQ(result->Trace, expected) << result->Ir;
}

std::shared_ptr<TVarStmt> Parameter(std::string name, TTypePtr type) {
    return std::make_shared<TVarStmt>(TLocation{}, std::move(name), std::move(type));
}

} // namespace

TEST(LifetimeLowering, RetainUsesRuntimeAbi) {
    auto parameter = Parameter("value", std::make_shared<TStringType>());
    auto retain = std::make_shared<TRetainExpr>(TLocation{}, Ident("value"));
    ExpectTrace(
        {parameter},
        {Destroy(retain)},
        "load arg call:str_retain arg call:str_release jmp ret");
}

TEST(LifetimeLowering, OwnLiteralUsesRuntimeAbi) {
    ExpectTrace(
        {},
        {Destroy(OwnedLiteral())},
        "arg call:str_from_lit arg call:str_release jmp ret");
}

TEST(LifetimeLowering, MoveAddsNoInstructions) {
    auto direct = Lower({}, {Destroy(OwnedLiteral("direct"))});
    ASSERT_TRUE(direct.has_value()) << direct.error().ToString();
    auto moved = Lower({}, {
        Destroy(std::make_shared<TMoveExpr>(
            TLocation{},
            OwnedLiteral("moved"))),
    });
    ASSERT_TRUE(moved.has_value()) << moved.error().ToString();
    EXPECT_EQ(moved->Trace, direct->Trace) << moved->Ir;
}

TEST(LifetimeLowering, BorrowAddsNoInstructions) {
    auto parameter = Parameter("value", std::make_shared<TStringType>());
    auto direct = Lower(
        {parameter},
        {Destroy(std::make_shared<TRetainExpr>(TLocation{}, Ident("value")))});
    ASSERT_TRUE(direct.has_value()) << direct.error().ToString();

    parameter = Parameter("value", std::make_shared<TStringType>());
    auto borrowed = Lower(
        {parameter},
        {Destroy(std::make_shared<TRetainExpr>(
            TLocation{},
            std::make_shared<TBorrowExpr>(TLocation{}, Ident("value"))))});
    ASSERT_TRUE(borrowed.has_value()) << borrowed.error().ToString();
    EXPECT_EQ(borrowed->Trace, direct->Trace) << borrowed->Ir;
}

TEST(LifetimeLowering, DestroyStringUsesRelease) {
    auto parameter = Parameter("value", std::make_shared<TStringType>());
    ExpectTrace(
        {parameter},
        {Destroy(Ident("value"))},
        "load arg call:str_release jmp ret");
}

TEST(LifetimeLowering, DestroyPlainArrayUsesArrayDestroy) {
    auto arrayType = std::make_shared<TArrayType>(
        std::make_shared<TIntegerType>(),
        1);
    auto parameter = Parameter("items", arrayType);
    ExpectTrace(
        {parameter},
        {Destroy(Ident("items"))},
        "load arg call:array_destroy jmp ret");
}

TEST(LifetimeLowering, DestroyStringArrayPassesAllocationSize) {
    auto arrayType = std::make_shared<TArrayType>(
        std::make_shared<TStringType>(),
        1);
    auto parameter = Parameter("items", arrayType);
    ExpectTrace(
        {parameter},
        {Destroy(
            Ident("items"),
            std::make_shared<TNumberExpr>(TLocation{}, int64_t{32}))},
        "load arg arg call:array_str_destroy jmp ret");
}

TEST(LifetimeLowering, DestroyStringArrayRequiresAllocationSize) {
    auto arrayType = std::make_shared<TArrayType>(
        std::make_shared<TStringType>(),
        1);
    auto parameter = Parameter("items", arrayType);
    auto result = Lower({parameter}, {Destroy(Ident("items"))});
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(
        result.error().ToString().find("requires an allocation-size operand"),
        std::string::npos);
}

TEST(LifetimeLowering, ReplaceEvaluatesRhsBeforeTargetAndDestroysOldValue) {
    auto parameter = Parameter(
        "target",
        std::make_shared<TReferenceType>(std::make_shared<TStringType>()));
    auto replace = std::make_shared<TReplaceExpr>(
        TLocation{},
        Ident("target"),
        OwnedLiteral("new value"));
    ExpectTrace(
        {parameter},
        {replace},
        "arg call:str_from_lit load lde arg call:str_release ste jmp ret");
}

TEST(LifetimeLowering, RewrittenSideEffectingIndexIsEvaluatedOnce) {
    auto pointerType = std::make_shared<TPointerType>(std::make_shared<TStringType>());
    auto parameter = Parameter("items", pointerType);
    auto nextIndex = std::make_shared<TCallExpr>(
        TLocation{},
        Ident("input_int64"),
        std::vector<TExprPtr>{});
    auto assignment = std::make_shared<TArrayAssignExpr>(
        TLocation{},
        "items",
        std::vector<TExprPtr>{nextIndex},
        std::make_shared<TStringLiteralExpr>(TLocation{}, "new value"));
    ExpectTrace(
        {parameter},
        {assignment},
        "arg call:str_from_lit call:input_int64 load * + lde arg call:str_release ste jmp ret");
}

TEST(LifetimeLowering, StructuredCleanupExitLowersItsActionsBeforeExit) {
    ExpectTrace({}, {
        std::make_shared<TCleanupExitExpr>(
            TLocation{},
            ECleanupExitKind::Return,
            nullptr,
            std::vector<TExprPtr>{Destroy(OwnedLiteral())}),
    }, "arg call:str_from_lit arg call:str_release jmp ret");
}

TEST(LifetimeLowering, GlobalCleanupBuildsModuleDestructorInLifoOrder) {
    auto stringType = std::make_shared<TStringType>();
    auto first = std::make_shared<TVarStmt>(TLocation{}, "first", stringType);
    first->Init = std::make_shared<TStringLiteralExpr>(TLocation{}, "first");
    auto plain = std::make_shared<TVarStmt>(
        TLocation{},
        "plain",
        std::make_shared<TArrayType>(std::make_shared<TIntegerType>(), 1));
    plain->Bounds.push_back({
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}),
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{2}),
    });
    auto words = std::make_shared<TVarStmt>(
        TLocation{},
        "words",
        std::make_shared<TArrayType>(stringType, 1));
    words->Bounds.push_back({
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{1}),
        std::make_shared<TNumberExpr>(TLocation{}, int64_t{2}),
    });
    TExprPtr root = std::make_shared<TBlockExpr>(
        TLocation{},
        std::vector<TExprPtr>{
            first,
            plain,
            words,
            Function({}, {}),
        });

    auto result = LowerNamedFunction(root, "$$module_destructor");
    ASSERT_TRUE(result.has_value()) << result.error().ToString();
    EXPECT_EQ(
        result->Trace,
        "load load arg arg call:array_str_destroy load arg "
        "call:array_destroy load arg call:str_release ret")
        << result->Ir;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
