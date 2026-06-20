#include <gtest/gtest.h>

#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/modules/system/system.h>
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

TEST(LifetimeLowering, StructuredCleanupNodesHaveDedicatedErrors) {
    auto exitResult = Lower({}, {
        std::make_shared<TCleanupExitExpr>(
            TLocation{},
            ECleanupExitKind::Return,
            nullptr,
            std::vector<TExprPtr>{}),
    });
    ASSERT_FALSE(exitResult.has_value());
    EXPECT_NE(
        exitResult.error().ToString().find("cleanup-exit lowering is not implemented"),
        std::string::npos);

    auto globalResult = Lower({}, {
        std::make_shared<TGlobalCleanupExpr>(
            TLocation{},
            std::vector<TExprPtr>{}),
    });
    ASSERT_FALSE(globalResult.has_value());
    EXPECT_NE(
        globalResult.error().ToString().find("cleanup-global lowering is not implemented"),
        std::string::npos);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
