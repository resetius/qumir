#include <gtest/gtest.h>

#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/parser/ast.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/runtime/io.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/eval.h>
#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_runner.h>

#include <sstream>

using namespace NQumir;
using namespace NQumir::NAst;

namespace {

struct TRunResult {
    std::string output;
    std::string error;
    std::string ir;
    bool ok() const { return error.empty(); }
};

enum class EBackend {
    VM,
    LLVM,
};

TRunResult RunWithInjection(
    const std::string& src,
    size_t insertAt,
    std::vector<TExprPtr> injected,
    EBackend backend = EBackend::VM)
{
    NSemantics::TNameResolver resolver;
    auto sysMod = std::make_shared<NRegistry::SystemModule>();
    resolver.RegisterModule(sysMod.get());
    resolver.ImportModule(sysMod->Name());

    std::istringstream in(src);
    TTokenStream ts(in);
    TParser p;
    auto parsed = p.parse(ts, &resolver);
    if (!parsed) return {{}, parsed.error().ToString()};
    auto ast = std::move(parsed.value());

    auto* topBlock = dynamic_cast<TBlockExpr*>(ast.get());
    if (!topBlock) return {{}, "expected TBlockExpr at top level"};

    TBlockExpr* funcBody = nullptr;
    for (auto& stmt : topBlock->Stmts) {
        if (auto* funDecl = dynamic_cast<TFunDecl*>(stmt.get())) {
            funcBody = funDecl->Body.get();
            break;
        }
    }
    if (!funcBody) return {{}, "expected TFunDecl in top-level block"};

    auto it = funcBody->Stmts.begin() + static_cast<ptrdiff_t>(insertAt);
    funcBody->Stmts.insert(it, injected.begin(), injected.end());

    auto scope = resolver.GetOrCreateRootScope();
    scope->AllowsRedeclare = true;
    scope->RootLevel = false;

    if (auto err = NTransform::Pipeline(ast, resolver); !err) {
        return {{}, err.error().ToString()};
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);
    NIR::TAstLowerer lowerer(module, builder, resolver);
    if (auto lowerRes = lowerer.LowerTop(ast); !lowerRes) {
        return {{}, lowerRes.error().ToString()};
    }

    auto* mainFun = module.GetEntryPoint();
    if (!mainFun) return {{}, "no main function"};

    std::ostringstream ir;
    module.Print(ir);

    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istringstream stdinIn;
    NRuntime::SetInputStream(&stdinIn);
    try {
        if (backend == EBackend::VM) {
            NIR::TInterpreter interp(module, out, stdinIn);
            interp.Eval(*mainFun, {}, {});
        } else {
            NCodeGen::TLLVMCodeGen cg({});
            auto artifacts = cg.Emit(module, 0);
            NCodeGen::TLlvmRunner runner;
            std::string runErr;
            runner.Run(std::move(artifacts), mainFun->Name, &runErr, mainFun->ReturnTypeIsString);
            if (!runErr.empty()) {
                NRuntime::SetOutputStream(nullptr);
                return {{}, std::string("llvm run error: ") + runErr, ir.str()};
            }
        }
    } catch (const std::exception& e) {
        NRuntime::SetOutputStream(nullptr);
        return {{}, std::string("runtime error: ") + e.what(), ir.str()};
    }
    NRuntime::SetOutputStream(nullptr);
    return {out.str(), {}, ir.str()};
}

TExprPtr num(int64_t value) {
    return std::make_shared<TNumberExpr>(TLocation{}, value);
}

TExprPtr fnum(double value) {
    return std::make_shared<TNumberExpr>(TLocation{}, value);
}

TExprPtr str(std::string value) {
    return std::make_shared<TStringLiteralExpr>(TLocation{}, std::move(value));
}

TExprPtr ifExpr(TExprPtr cond, TExprPtr thenExpr, TExprPtr elseExpr) {
    return std::make_shared<TIfExpr>(
        TLocation{},
        std::move(cond),
        std::move(thenExpr),
        std::move(elseExpr));
}

TExprPtr assign(const std::string& name, TExprPtr value) {
    return std::make_shared<TAssignExpr>(TLocation{}, name, std::move(value));
}

TRunResult RunIntIfExpr(TExprPtr expr, EBackend backend = EBackend::VM) {
    const std::string src = "алг\nнач\n  цел x\n  вывод x, нс\nкон\n";
    return RunWithInjection(src, 1, {assign("x", std::move(expr))}, backend);
}

} // namespace

TEST(IfExpr, VMExecutesThenBranch) {
    auto result = RunIntIfExpr(ifExpr(num(1), num(10), num(20)));
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.output, "10\n");
}

TEST(IfExpr, VMExecutesElseBranch) {
    auto result = RunIntIfExpr(ifExpr(num(0), num(10), num(20)));
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_EQ(result.output, "20\n");
}

TEST(IfExpr, LLVMExecutesBranches) {
    auto thenResult = RunIntIfExpr(ifExpr(num(1), num(10), num(20)), EBackend::LLVM);
    ASSERT_TRUE(thenResult.ok()) << thenResult.error;
    EXPECT_EQ(thenResult.output, "10\n");

    auto elseResult = RunIntIfExpr(ifExpr(num(0), num(10), num(20)), EBackend::LLVM);
    ASSERT_TRUE(elseResult.ok()) << elseResult.error;
    EXPECT_EQ(elseResult.output, "20\n");
}

TEST(IfExpr, LoweringEmitsPhi) {
    auto result = RunIntIfExpr(ifExpr(num(1), num(10), num(20)));
    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_NE(result.ir.find("phi"), std::string::npos) << result.ir;
}

TEST(IfExpr, TypeAnnotationCastsNumericBranchesToCommonType) {
    const std::string src = "алг\nнач\n  вещ x\n  вывод x, нс\nкон\n";
    auto result = RunWithInjection(src, 1, {
        assign("x", ifExpr(num(1), num(7), fnum(2.5))),
    });

    ASSERT_TRUE(result.ok()) << result.error;
    EXPECT_NE(result.ir.find("phi"), std::string::npos) << result.ir;
    EXPECT_NE(result.ir.find("f64"), std::string::npos) << result.ir;
    EXPECT_NE(result.output.find("7"), std::string::npos) << result.output;
}

TEST(IfExpr, TypeAnnotationRejectsMissingElse) {
    auto result = RunIntIfExpr(ifExpr(num(1), num(10), nullptr));
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("if-expression"), std::string::npos) << result.error;
}

TEST(IfExpr, TypeAnnotationRejectsIncompatibleBranches) {
    auto result = RunIntIfExpr(ifExpr(num(1), num(10), str("bad")));
    EXPECT_FALSE(result.ok());
    EXPECT_NE(result.error.find("if-expression"), std::string::npos) << result.error;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
