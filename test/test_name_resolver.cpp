#include <gtest/gtest.h>

#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <sstream>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NSemantics;

static TExprPtr parseStmtList(const std::string& src) {
    std::istringstream in(src);
    TTokenStream ts(in);
    TParser p;
    auto res = p.parse(ts);
    if (!res.has_value()) {
        std::cerr << "Parse error: " << res.error().ToString() << std::endl;
        return nullptr;
    }
    return std::move(res.value());
}

TEST(NameResolver, DeclBindsSymbolIds) {
    auto ast = parseStmtList(R"__(
цел a, b, c
a := 10
b := 10
)__");
    ASSERT_NE(ast, nullptr);

    TNameResolver r{};
    r.Resolve(ast);

    const auto& syms = r.GetSymbols();
    ASSERT_EQ(syms.size(), 3u);

    // Check decl bindings exist
    const auto* root = dynamic_cast<TBlockExpr*>(ast.get());
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(root->Stmts.size(), 5u);

    auto declA = TMaybeNode<TVarStmt>(root->Stmts[0]).Cast();
    auto declB = TMaybeNode<TVarStmt>(root->Stmts[1]).Cast();
    auto varC  = TMaybeNode<TVarStmt>(root->Stmts[2]).Cast();
    ASSERT_TRUE(declA && declB && varC);

    auto aId = r.Lookup(declA->Name, {0});
    auto bId = r.Lookup(declB->Name, {0});
    auto cId = r.Lookup(varC->Name, {0});

    ASSERT_TRUE(aId.has_value());
    ASSERT_TRUE(bId.has_value());
    ASSERT_TRUE(cId.has_value());

    // Names and mutability
    EXPECT_EQ(syms[aId->Id].Name, "a");
    EXPECT_EQ(syms[bId->Id].Name, "b");
    EXPECT_EQ(syms[cId->Id].Name, "c");
}

TEST(NameResolver, Scopes) {
    auto ast = parseStmtList(R"__(
цел a, b, c
a := 10
b := 10
алг тест1 нач
    цел a, b, c
    a := 10
    b := 20
    c := 30
кон
алг тест2 нач
    цел a, b, c
    a := 1
    b := 2
    c := 3
кон
)__");

    ASSERT_NE(ast, nullptr);

    TNameResolver r{};
    r.Resolve(ast);

    auto s1 = r.Lookup("a", {0});
    auto s2 = r.Lookup("b", {0});
    auto s3 = r.Lookup("c", {0});
    EXPECT_EQ(s1->Id, 2);
    EXPECT_EQ(s2->Id, 3);
    EXPECT_EQ(s3->Id, 4);
    EXPECT_EQ(s1->ScopeLevelIdx, 2);
    EXPECT_EQ(s2->ScopeLevelIdx, 3);
    EXPECT_EQ(s3->ScopeLevelIdx, 4);

    auto s11 = r.Lookup("a", {3});
    auto s12 = r.Lookup("b", {3});
    auto s13 = r.Lookup("c", {3});
    EXPECT_EQ(s11->Id, 5);
    EXPECT_EQ(s12->Id, 6);
    EXPECT_EQ(s13->Id, 7);
    EXPECT_EQ(s11->ScopeLevelIdx, 0);
    EXPECT_EQ(s12->ScopeLevelIdx, 1);
    EXPECT_EQ(s13->ScopeLevelIdx, 2);
    EXPECT_EQ(s11->FunctionLevelIdx, 0);
    EXPECT_EQ(s12->FunctionLevelIdx, 1);
    EXPECT_EQ(s13->FunctionLevelIdx, 2);

    auto s21 = r.Lookup("a", {4});
    auto s22 = r.Lookup("b", {4});
    auto s23 = r.Lookup("c", {4});
    EXPECT_EQ(s21->Id, 8);
    EXPECT_EQ(s22->Id, 9);
    EXPECT_EQ(s23->Id, 10);
    EXPECT_EQ(s21->ScopeLevelIdx, 0);
    EXPECT_EQ(s22->ScopeLevelIdx, 1);
    EXPECT_EQ(s23->ScopeLevelIdx, 2);
    EXPECT_EQ(s21->FunctionLevelIdx, 0);
    EXPECT_EQ(s22->FunctionLevelIdx, 1);
    EXPECT_EQ(s23->FunctionLevelIdx, 2);

    for (auto& sym : r.GetSymbols()) {
        std::cout << "Symbol: " << sym.Name
                  << ", Id: " << sym.Id.Id
                  << ", ScopeId: " << sym.ScopeId.Id
                  << ", ScopeLevelIdx: " << sym.ScopeLevelIdx
                  << ", FunctionLevelIdx: " << sym.FunctionLevelIdx
                  << ", FuncScopeId: " << sym.FuncScopeId.Id
                  << "\n";
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
