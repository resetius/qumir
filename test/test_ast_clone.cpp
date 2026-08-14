#include <gtest/gtest.h>

#include <qumir/parser/ast.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;

namespace {

void ExpectDisjointChildren(const TExprPtr& original, const TExprPtr& clone) {
    ASSERT_EQ(static_cast<bool>(original), static_cast<bool>(clone));
    if (!original) {
        return;
    }
    EXPECT_NE(original.get(), clone.get());
    const auto originalChildren = original->Children();
    const auto cloneChildren = clone->Children();
    ASSERT_EQ(originalChildren.size(), cloneChildren.size());
    for (size_t index = 0; index < originalChildren.size(); ++index) {
        ExpectDisjointChildren(originalChildren[index], cloneChildren[index]);
    }
}

} // namespace

TEST(AstClone, LifetimeAndHiddenChildrenAreDeeplyCloned) {
    TLocation loc{};
    auto ident = [&](std::string name) {
        return std::make_shared<TIdentExpr>(loc, std::move(name));
    };

    auto variable = std::make_shared<TVarStmt>(
        loc, "variable", std::make_shared<TIntegerType>());
    variable->Init = ident("variable_init");
    auto vars = std::make_shared<TVarsBlockExpr>(
        loc, std::vector<TExprPtr>{variable});

    auto parameter = std::make_shared<TVarStmt>(
        loc, "parameter", std::make_shared<TIntegerType>());
    parameter->Init = ident("parameter_init");
    auto function = std::make_shared<TFunDecl>(
        loc,
        "function",
        std::vector<TGenericParam>{},
        std::vector<TParam>{parameter},
        std::make_shared<TBlockExpr>(
            loc, std::vector<TExprPtr>{ident("function_body")}),
        std::make_shared<TVoidType>());

    auto original = std::make_shared<TBlockExpr>(loc, std::vector<TExprPtr>{
        std::make_shared<TRetainExpr>(loc, ident("retain")),
        std::make_shared<TOwnLiteralExpr>(loc, ident("own")),
        std::make_shared<TMoveExpr>(loc, ident("move")),
        std::make_shared<TBorrowExpr>(loc, ident("borrow")),
        std::make_shared<TDestroyExpr>(loc, ident("destroy"), ident("aux")),
        std::make_shared<TReplaceExpr>(loc, ident("target"), ident("value")),
        std::make_shared<TCleanupExitExpr>(
            loc,
            ECleanupExitKind::Return,
            ident("return"),
            std::vector<TExprPtr>{ident("cleanup")}),
        std::make_shared<TGlobalCleanupExpr>(
            loc, std::vector<TExprPtr>{ident("global_cleanup")}),
        vars,
        function,
    });

    auto clone = TMaybeNode<TBlockExpr>(DeepCloneExpr(original)).Cast();
    ASSERT_NE(clone, nullptr);
    ExpectDisjointChildren(original, clone);

    auto clonedVars = TMaybeNode<TVarsBlockExpr>(clone->Stmts[8]).Cast();
    ASSERT_NE(clonedVars, nullptr);
    ASSERT_EQ(clonedVars->Vars.size(), 1u);
    ExpectDisjointChildren(variable, clonedVars->Vars[0]);

    auto clonedFunction = TMaybeNode<TFunDecl>(clone->Stmts[9]).Cast();
    ASSERT_NE(clonedFunction, nullptr);
    ASSERT_EQ(clonedFunction->Params.size(), 1u);
    ExpectDisjointChildren(parameter, clonedFunction->Params[0]);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
