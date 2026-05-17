#include <gtest/gtest.h>

#include <qumir/modules/robot/robot.h>
#include <qumir/modules/turtle/turtle.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>
#include <qumir/semantics/transform/transform.h>
#include <sstream>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NRegistry;
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

static const TExternalFunction* findExternal(const std::vector<TExternalFunction>& functions, const std::string& mangledName) {
    for (const auto& function : functions) {
        if (function.MangledName == mangledName) {
            return &function;
        }
    }
    return nullptr;
}

static void expectMaySuspend(const std::vector<TExternalFunction>& functions, const std::string& mangledName, bool maySuspend) {
    auto* function = findExternal(functions, mangledName);
    ASSERT_NE(function, nullptr) << mangledName;
    EXPECT_EQ(function->MaySuspend, maySuspend) << mangledName;
}

static std::shared_ptr<TFunDecl> findFunction(const TExprPtr& ast, const std::string& name) {
    auto block = TMaybeNode<TBlockExpr>(ast).Cast();
    if (!block) {
        return nullptr;
    }
    for (const auto& stmt : block->Stmts) {
        auto fun = TMaybeNode<TFunDecl>(stmt).Cast();
        if (fun && fun->Name == name) {
            return fun;
        }
    }
    return nullptr;
}

static std::shared_ptr<TCallExpr> findCall(const TExprPtr& expr, const std::string& name) {
    if (!expr) {
        return nullptr;
    }
    if (auto call = TMaybeNode<TCallExpr>(expr).Cast()) {
        if (auto ident = TMaybeNode<TIdentExpr>(call->Callee).Cast(); ident && ident->Name == name) {
            return call;
        }
    }
    for (const auto& child : expr->Children()) {
        if (auto found = findCall(child, name)) {
            return found;
        }
    }
    return nullptr;
}

static std::shared_ptr<TAwaitExpr> findAwaitCall(const TExprPtr& expr, const std::string& name) {
    if (!expr) {
        return nullptr;
    }
    if (auto awaitExpr = TMaybeNode<TAwaitExpr>(expr).Cast()) {
        auto call = TMaybeNode<TCallExpr>(awaitExpr->Operand).Cast();
        if (call) {
            if (auto ident = TMaybeNode<TIdentExpr>(call->Callee).Cast(); ident && ident->Name == name) {
                return awaitExpr;
            }
        }
    }
    for (const auto& child : expr->Children()) {
        if (auto found = findAwaitCall(child, name)) {
            return found;
        }
    }
    return nullptr;
}

static TExprPtr annotateWithRobotCoroutines(const std::string& src) {
    RobotModule robot;
    TNameResolver resolver;
    resolver.RegisterModule(&robot);
    auto importResult = resolver.ImportModule(robot.Name());
    if (!importResult) {
        ADD_FAILURE() << importResult.error();
        return nullptr;
    }

    std::istringstream in(src);
    TTokenStream ts(in);
    TParser parser;
    auto parsed = parser.parse(ts, &resolver);
    if (!parsed) {
        ADD_FAILURE() << parsed.error().ToString();
        return nullptr;
    }
    auto ast = std::move(parsed.value());
    auto result = NTransform::Pipeline(
        ast,
        resolver,
        NTransform::TPipelineOptions{.EnableCoroutineAnalysis = true});
    if (!result) {
        ADD_FAILURE() << result.error().ToString();
        return nullptr;
    }
    return ast;
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

TEST(TypeAnnotation, CoroutineAnalysisMarksDirectAndTransitiveCallers) {
    auto ast = annotateWithRobotCoroutines(R"__(
алг helper
нач
    вверх()
кон

алг caller
нач
    helper()
кон

алг pure
нач
кон
)__");
    ASSERT_NE(ast, nullptr);

    auto helper = findFunction(ast, "helper");
    auto caller = findFunction(ast, "caller");
    auto pure = findFunction(ast, "pure");
    ASSERT_TRUE(helper);
    ASSERT_TRUE(caller);
    ASSERT_TRUE(pure);

    auto helperFuture = TMaybeType<TFutureType>(helper->RetType).Cast();
    auto callerFuture = TMaybeType<TFutureType>(caller->RetType).Cast();
    ASSERT_TRUE(helperFuture);
    ASSERT_TRUE(callerFuture);
    EXPECT_TRUE(TMaybeType<TVoidType>(helperFuture->ResultType));
    EXPECT_TRUE(TMaybeType<TVoidType>(callerFuture->ResultType));
    EXPECT_FALSE(TMaybeType<TFutureType>(pure->RetType));
}

TEST(TypeAnnotation, CoroutineAwaitUnwrapsReturnValueAtCallSite) {
    auto ast = annotateWithRobotCoroutines(R"__(
алг entry
нач
    цел value
    value := wrap()
кон

алг цел wrap
нач
    закрасить()
    знач := 12
кон
)__");
    ASSERT_NE(ast, nullptr);

    auto wrap = findFunction(ast, "wrap");
    ASSERT_TRUE(wrap);
    auto wrapFuture = TMaybeType<TFutureType>(wrap->RetType).Cast();
    ASSERT_TRUE(wrapFuture);
    EXPECT_TRUE(TMaybeType<TIntegerType>(wrapFuture->ResultType));

    auto entry = findFunction(ast, "entry");
    ASSERT_TRUE(entry);
    auto awaitCall = findAwaitCall(entry->Body, "wrap");
    ASSERT_TRUE(awaitCall);
    EXPECT_TRUE(TMaybeType<TIntegerType>(awaitCall->Type));
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
алг цел тест(цел x,y,z) нач
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
    EXPECT_EQ(s1->ScopeLevelIdx, 3);
    EXPECT_EQ(s2->ScopeLevelIdx, 4);
    EXPECT_EQ(s3->ScopeLevelIdx, 5);

    auto s11 = r.Lookup("a", {4});
    auto s12 = r.Lookup("b", {4});
    auto s13 = r.Lookup("c", {4});
    EXPECT_EQ(s11->ScopeLevelIdx, 0);
    EXPECT_EQ(s12->ScopeLevelIdx, 1);
    EXPECT_EQ(s13->ScopeLevelIdx, 2);
    EXPECT_EQ(s11->FunctionLevelIdx, 0);
    EXPECT_EQ(s12->FunctionLevelIdx, 1);
    EXPECT_EQ(s13->FunctionLevelIdx, 2);

    auto s21 = r.Lookup("a", {5});
    auto s22 = r.Lookup("b", {5});
    auto s23 = r.Lookup("c", {5});
    EXPECT_EQ(s21->ScopeLevelIdx, 0);
    EXPECT_EQ(s22->ScopeLevelIdx, 1);
    EXPECT_EQ(s23->ScopeLevelIdx, 2);
    EXPECT_EQ(s21->FunctionLevelIdx, 0);
    EXPECT_EQ(s22->FunctionLevelIdx, 1);
    EXPECT_EQ(s23->FunctionLevelIdx, 2);

    auto x = r.Lookup("x", {3});
    auto y = r.Lookup("y", {3});
    auto z = r.Lookup("z", {3});
    EXPECT_EQ(x->FunctionLevelIdx, 0);
    EXPECT_EQ(y->FunctionLevelIdx, 1);
    EXPECT_EQ(z->FunctionLevelIdx, 2);
    auto __return = r.Lookup("$$return", {6});
    auto a = r.Lookup("a", {6});
    auto b = r.Lookup("b", {6});
    auto c = r.Lookup("c", {6});
    EXPECT_EQ(__return->FunctionLevelIdx, 3);
    EXPECT_EQ(a->FunctionLevelIdx, 4);
    EXPECT_EQ(b->FunctionLevelIdx, 5);
    EXPECT_EQ(c->FunctionLevelIdx, 6);

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

TEST(EditDistance, StringIdentical) {
    TEditDistance ed;
    std::string a = "hello";
    std::string b = "hello";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 0);
}

TEST(EditDistance, StringOneInsertion) {
    TEditDistance ed;
    std::string a = "hello";
    std::string b = "helo";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, StringOneDeletion) {
    TEditDistance ed;
    std::string a = "helo";
    std::string b = "hello";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, StringOneSubstitution) {
    TEditDistance ed;
    std::string a = "hello";
    std::string b = "hallo";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, StringMultipleOperations) {
    TEditDistance ed;
    std::string a = "kitten";
    std::string b = "sitting";
    // kitten -> sitten (substitute k->s)
    // sitten -> sittin (substitute e->i)
    // sittin -> sitting (insert g)
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 3);
}

TEST(EditDistance, StringEmpty) {
    TEditDistance ed;
    std::string a = "hello";
    std::string b = "";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 5);
    EXPECT_EQ(ed.Calc<char>(std::span(b.data(), b.size()), std::span(a.data(), a.size())), 5);
}

TEST(EditDistance, StringBothEmpty) {
    TEditDistance ed;
    std::string a = "";
    std::string b = "";
    EXPECT_EQ(ed.Calc<char>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 0);
}

TEST(EditDistance, IntArrayIdentical) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {1, 2, 3, 4, 5};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 0);
}

TEST(EditDistance, IntArrayOneInsertion) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {1, 2, 4, 5};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, IntArrayOneDeletion) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 4, 5};
    std::vector<int> b = {1, 2, 3, 4, 5};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, IntArrayOneSubstitution) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {1, 2, 9, 4, 5};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 1);
}

TEST(EditDistance, IntArrayMultipleOperations) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {4, 5, 6, 7};
    // All elements need to be changed plus one insertion
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 4);
}

TEST(EditDistance, IntArrayEmpty) {
    TEditDistance ed;
    std::vector<int> a = {1, 2, 3, 4, 5};
    std::vector<int> b = {};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 5);
    EXPECT_EQ(ed.Calc<int>(std::span(b.data(), b.size()), std::span(a.data(), a.size())), 5);
}

TEST(EditDistance, IntArrayBothEmpty) {
    TEditDistance ed;
    std::vector<int> a = {};
    std::vector<int> b = {};
    EXPECT_EQ(ed.Calc<int>(std::span(a.data(), a.size()), std::span(b.data(), b.size())), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
