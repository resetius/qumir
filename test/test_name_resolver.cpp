#include <gtest/gtest.h>

#include <qumir/modules/robot/robot.h>
#include <qumir/modules/system/system.h>
#include <qumir/modules/turtle/turtle.h>
#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>
#include <qumir/semantics/transform/transform.h>
#include <sstream>
#include <optional>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NRegistry;
using namespace NQumir::NSemantics;

namespace {

TExprPtr parseStmtList(const std::string& src) {
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

std::shared_ptr<TFunDecl> findFunction(const TExprPtr& ast, const std::string& name) {
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

std::shared_ptr<TCallExpr> findCall(const TExprPtr& expr, const std::string& name) {
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

std::shared_ptr<TAwaitExpr> findAwaitCall(const TExprPtr& expr, const std::string& name) {
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

TExprPtr annotateWithRobotCoroutines(const std::string& src) {
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
        NTransform::TPipelineOptions{.Extensions = {
            .AfterTypeAnnotation = {NTransform::CoroutineAnnotationTransform},
        }});
    if (!result) {
        ADD_FAILURE() << result.error().ToString();
        return nullptr;
    }
    return ast;
}

// Synthetic module exercising GENERICS_PLAN.md, этап 5: a module can export
// an external function whose signature mentions a `template` placeholder
// type. There is no native implementation to point Ptr/Packed at for an
// arbitrary concrete type, so — exactly like the realistic path documented
// at the этап-5 instantiation branch — it is type-agnostic and expressed via
// an Inline factory (a transparent passthrough), which the transform pass
// splices into every call site before lowering ever sees the function.
class GenericExternalModule : public IModule {
public:
    GenericExternalModule() {
        auto placeholder = std::make_shared<TNamedType>("K", nullptr);
        placeholder->Template = true;
        placeholder->Readable = true;
        placeholder->Mutable = true;

        ExternalFunctions_ = {
            {
                .Name = "extId",
                .MangledName = "ext_id",
                .ArgTypes = { placeholder },
                .ReturnType = placeholder,
                .Inline = [](std::vector<TExprPtr> args) -> TExprPtr {
                    return args[0];
                },
            },
        };
    }

    const std::string& Name() const override {
        static const std::string name = "GenericExternalTest";
        return name;
    }
    const std::vector<TExternalFunction>& ExternalFunctions() const override { return ExternalFunctions_; }
    const std::vector<TExternalType>& ExternalTypes() const override { return ExternalTypes_; }
    const std::vector<TLiteralSuffix>& LiteralSuffixes() const override { return LiteralSuffixes_; }
    const std::vector<std::string>& Dependencies() const override { return Dependencies_; }

private:
    std::vector<TExternalFunction> ExternalFunctions_;
    std::vector<TExternalType> ExternalTypes_;
    std::vector<TLiteralSuffix> LiteralSuffixes_;
    std::vector<std::string> Dependencies_;
};

std::optional<NIR::TModule> buildRobotCoroutineModule(const std::string& src) {
    RobotModule robot;
    TNameResolver resolver;
    resolver.RegisterModule(&robot);
    auto importResult = resolver.ImportModule(robot.Name());
    if (!importResult) {
        ADD_FAILURE() << importResult.error();
        return std::nullopt;
    }

    std::istringstream in(src);
    TTokenStream ts(in);
    TParser parser;
    auto parsed = parser.parse(ts, &resolver);
    if (!parsed) {
        ADD_FAILURE() << parsed.error().ToString();
        return std::nullopt;
    }
    auto ast = std::move(parsed.value());
    auto result = NTransform::Pipeline(
        ast,
        resolver,
        NTransform::TPipelineOptions{.Extensions = {
            .AfterTypeAnnotation = {NTransform::CoroutineAnnotationTransform},
        }});
    if (!result) {
        ADD_FAILURE() << result.error().ToString();
        return std::nullopt;
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);
    NIR::TAstLowerer lowerer(module, builder, resolver);
    auto lowered = lowerer.LowerTop(ast);
    if (!lowered) {
        ADD_FAILURE() << lowered.error().ToString();
        return std::nullopt;
    }

    return module;
}

std::optional<NIR::TModule> buildTurtleCoroutineModule(const std::string& src) {
    TurtleModule turtle;
    TNameResolver resolver;
    resolver.RegisterModule(&turtle);
    auto importResult = resolver.ImportModule(turtle.Name());
    if (!importResult) {
        ADD_FAILURE() << importResult.error();
        return std::nullopt;
    }

    std::istringstream in(src);
    TTokenStream ts(in);
    TParser parser;
    auto parsed = parser.parse(ts, &resolver);
    if (!parsed) {
        ADD_FAILURE() << parsed.error().ToString();
        return std::nullopt;
    }
    auto ast = std::move(parsed.value());
    auto result = NTransform::Pipeline(
        ast,
        resolver,
        NTransform::TPipelineOptions{.Extensions = {
            .AfterTypeAnnotation = {NTransform::CoroutineAnnotationTransform},
        }});
    if (!result) {
        ADD_FAILURE() << result.error().ToString();
        return std::nullopt;
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);
    NIR::TAstLowerer lowerer(module, builder, resolver);
    auto lowered = lowerer.LowerTop(ast);
    if (!lowered) {
        ADD_FAILURE() << lowered.error().ToString();
        return std::nullopt;
    }

    return module;
}

std::string lowerRobotCoroutineIR(const std::string& src) {
    auto module = buildRobotCoroutineModule(src);
    if (!module) {
        return {};
    }

    std::ostringstream out;
    module->Print(out);
    return out.str();
}

std::string emitRobotCoroutineLLVM(const std::string& src, int optLevel = 0) {
    auto module = buildRobotCoroutineModule(src);
    if (!module) {
        return {};
    }

    std::optional<NCodeGen::TLLVMInitializer> llvmInit;
    if (optLevel > 0) {
        llvmInit.emplace();
    }
    NCodeGen::TLLVMCodeGen codegen;
    auto artifacts = codegen.Emit(*module, optLevel);
    std::ostringstream out;
    artifacts->PrintModule(out);
    return out.str();
}

std::string emitTurtleCoroutineLLVM(const std::string& src) {
    auto module = buildTurtleCoroutineModule(src);
    if (!module) {
        return {};
    }

    NCodeGen::TLLVMCodeGen codegen;
    auto artifacts = codegen.Emit(*module);
    std::ostringstream out;
    artifacts->PrintModule(out);
    return out.str();
}

} // namespace

TEST(TypeAnnotation, GenericExternalModuleFunctionInstantiatesPerCallSite) {
    // GENERICS_PLAN.md, этап 5: a module-registered external function with a
    // `template` placeholder in its signature must monomorphize at call
    // sites exactly like a user-written generic — producing one bodyless
    // concrete clone per distinct argument type, each carrying over the
    // original's MangledName/Ptr/Packed/InlineFactory. Unlike user-written
    // generics (which gain an annotated body and get spliced into the
    // top-level Stmts for lowering — GetGenericInstantiations), bodyless
    // clones are registered straight into the symbol table via
    // DeclareFunction, exactly like ImportModule registers ordinary external
    // functions: GetExternalFunctions/ImportExternalFunctions discover them
    // there, keyed by symbol id, with no AST presence required.
    GenericExternalModule mod;
    SystemModule system; // вывод(...) lowers to output_int64/output_string/... from this module
    TNameResolver resolver;
    resolver.RegisterModule(&mod);
    resolver.RegisterModule(&system);
    ASSERT_TRUE(resolver.ImportModule(mod.Name()));
    ASSERT_TRUE(resolver.ImportModule(system.Name()));

    auto src = R"__(
алг
нач
    вывод extId(5), нс
    вывод extId("hello"), нс
кон
)__";
    std::istringstream in(src);
    TTokenStream ts(in);
    TParser parser;
    auto parsed = parser.parse(ts, &resolver);
    ASSERT_TRUE(parsed) << parsed.error().ToString();
    auto ast = std::move(parsed.value());
    auto pipelineResult = NTransform::Pipeline(ast, resolver, NTransform::TPipelineOptions{});
    ASSERT_TRUE(pipelineResult) << pipelineResult.error().ToString();

    auto lookupFunDecl = [&](const std::string& name) -> std::shared_ptr<TFunDecl> {
        auto sym = resolver.Lookup(name, NSemantics::TScopeId{0});
        if (!sym) {
            return nullptr;
        }
        return TMaybeNode<TFunDecl>(resolver.GetSymbolNode(NSemantics::TSymbolId{sym->Id})).Cast();
    };

    auto intClone = lookupFunDecl("__generic_extId$Int");
    auto stringClone = lookupFunDecl("__generic_extId$String");
    ASSERT_TRUE(intClone) << "expected a monomorphized clone for extId(i64)";
    ASSERT_TRUE(stringClone) << "expected a monomorphized clone for extId(string)";
    EXPECT_NE(intClone, stringClone);

    // Bodyless external clones (этап 5's dedicated branch in
    // InstantiateGenericFunction), not annotated user-function clones —
    // carrying over MangledName/InlineFactory from the template declaration.
    EXPECT_EQ(intClone->Body, nullptr);
    EXPECT_EQ(stringClone->Body, nullptr);
    EXPECT_EQ(intClone->MangledName, "ext_id");
    EXPECT_EQ(stringClone->MangledName, "ext_id");
    ASSERT_TRUE(intClone->InlineFactory.has_value());
    ASSERT_TRUE(stringClone->InlineFactory.has_value());

    auto intFunType = TMaybeType<TFunctionType>(intClone->Type).Cast();
    ASSERT_TRUE(intFunType);
    ASSERT_EQ(intFunType->ParamTypes.size(), 1u);
    EXPECT_TRUE(TMaybeType<TIntegerType>(intFunType->ParamTypes[0]));
    EXPECT_TRUE(TMaybeType<TIntegerType>(intClone->RetType));

    auto stringFunType = TMaybeType<TFunctionType>(stringClone->Type).Cast();
    ASSERT_TRUE(stringFunType);
    ASSERT_EQ(stringFunType->ParamTypes.size(), 1u);
    EXPECT_TRUE(TMaybeType<TStringType>(stringFunType->ParamTypes[0]));
    EXPECT_TRUE(TMaybeType<TStringType>(stringClone->RetType));

    // GetExternalFunctions must surface both clones for lowering to import —
    // the same mechanism that picks up ordinary module-registered builtins.
    auto externs = resolver.GetExternalFunctions();
    auto hasExternal = [&](const std::shared_ptr<TFunDecl>& fn) {
        for (auto& [symId, decl] : externs) {
            if (decl == fn) return true;
        }
        return false;
    };
    EXPECT_TRUE(hasExternal(intClone));
    EXPECT_TRUE(hasExternal(stringClone));

    // Inline substitution (transform.cpp's PostTypeAnnotationTransform) must
    // have spliced the factory's passthrough AST into both call sites —
    // `вывод extId(5)` becomes `вывод 5`, `вывод extId("hello")` becomes
    // `вывод "hello"` — leaving no call to the clone for lowering to resolve.
    EXPECT_FALSE(findCall(ast, "__generic_extId$Int"));
    EXPECT_FALSE(findCall(ast, "__generic_extId$String"));
    EXPECT_FALSE(findCall(ast, "extId"));

    auto main = findFunction(ast, "<main>");
    ASSERT_TRUE(main);
    std::ostringstream rendered;
    rendered << TExprPtr(main->Body);
    EXPECT_NE(rendered.str().find("output_int64 5"), std::string::npos)
        << "expected extId(5) to be inlined to a bare 5:\n" << rendered.str();
    EXPECT_NE(rendered.str().find("output_string \"hello\""), std::string::npos)
        << "expected extId(\"hello\") to be inlined to a bare \"hello\":\n" << rendered.str();
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

    auto robotAwait = findAwaitCall(helper->Body, "вверх");
    auto helperAwait = findAwaitCall(caller->Body, "helper");
    ASSERT_TRUE(robotAwait);
    ASSERT_TRUE(helperAwait);
    EXPECT_TRUE(TMaybeType<TVoidType>(robotAwait->Type));
    EXPECT_TRUE(TMaybeType<TVoidType>(helperAwait->Type));

    auto robotCall = TMaybeNode<TCallExpr>(robotAwait->Operand).Cast();
    ASSERT_TRUE(robotCall);
    auto robotCalleeType = TMaybeType<TFunctionType>(robotCall->Callee->Type).Cast();
    ASSERT_TRUE(robotCalleeType);
    EXPECT_TRUE(TMaybeType<TFutureType>(robotCalleeType->ReturnType));
}

TEST(TypeAnnotation, CoroutineCallProducesInnerValueType) {
    auto ast = annotateWithRobotCoroutines(R"__(
алг цел f
нач
    вверх()
    знач := 42
кон

алг цел g
нач
    знач := f()
кон
)__");
    ASSERT_NE(ast, nullptr);

    auto f = findFunction(ast, "f");
    auto g = findFunction(ast, "g");
    ASSERT_TRUE(f);
    ASSERT_TRUE(g);

    auto fFuture = TMaybeType<TFutureType>(f->RetType).Cast();
    auto gFuture = TMaybeType<TFutureType>(g->RetType).Cast();
    ASSERT_TRUE(fFuture);
    ASSERT_TRUE(gFuture);
    EXPECT_TRUE(TMaybeType<TIntegerType>(fFuture->ResultType));
    EXPECT_TRUE(TMaybeType<TIntegerType>(gFuture->ResultType));

    auto awaitCall = findAwaitCall(g->Body, "f");
    ASSERT_TRUE(awaitCall);
    EXPECT_TRUE(TMaybeType<TIntegerType>(awaitCall->Type));
}

TEST(TypeAnnotation, NestedCoroutineCallArgumentIsAwaited) {
    auto ast = annotateWithRobotCoroutines(R"__(
алг цел f
нач
    вверх()
    знач := 7
кон

алг цел h(цел x)
нач
    знач := x + 1
кон

алг цел g
нач
    знач := h(f())
кон
)__");
    ASSERT_NE(ast, nullptr);

    auto f = findFunction(ast, "f");
    auto h = findFunction(ast, "h");
    auto g = findFunction(ast, "g");
    ASSERT_TRUE(f);
    ASSERT_TRUE(h);
    ASSERT_TRUE(g);

    EXPECT_TRUE(TMaybeType<TFutureType>(f->RetType));
    EXPECT_FALSE(TMaybeType<TFutureType>(h->RetType));
    EXPECT_TRUE(TMaybeType<TFutureType>(g->RetType));

    auto fAwait = findAwaitCall(g->Body, "f");
    auto hCall = findCall(g->Body, "h");
    ASSERT_TRUE(fAwait);
    ASSERT_TRUE(hCall);
    EXPECT_FALSE(findAwaitCall(g->Body, "h"));
    EXPECT_TRUE(TMaybeType<TIntegerType>(fAwait->Type));
    EXPECT_TRUE(TMaybeType<TIntegerType>(hCall->Type));
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

TEST(TypeAnnotation, FutureVoidLowersToCoroutineIRShape) {
    auto ir = lowerRobotCoroutineIR(R"__(
алг helper
нач
    вверх()
кон
)__");

    ASSERT_FALSE(ir.empty());
    EXPECT_NE(ir.find("function helper () { ; ptr to void coroutine result void"), std::string::npos) << ir;
    EXPECT_NE(ir.find("= вверх"), std::string::npos) << ir;
    EXPECT_NE(ir.find("await tmp"), std::string::npos) << ir;
}

TEST(TypeAnnotation, FutureValueCoroutineAwaitsChildResultInInitializer) {
    auto ir = lowerRobotCoroutineIR(R"__(
алг цел caller
нач
    знач := value()
кон

алг цел value
нач
    вверх()
    знач := 12
кон
)__");

    ASSERT_FALSE(ir.empty());
    EXPECT_NE(ir.find("function caller () { ; ptr to void coroutine result i64"), std::string::npos) << ir;
    EXPECT_NE(ir.find("function value () { ; ptr to void coroutine result i64"), std::string::npos) << ir;
    EXPECT_NE(ir.find("await tmp"), std::string::npos) << ir;
    EXPECT_NE(ir.find("= value"), std::string::npos) << ir;
}

TEST(TypeAnnotation, FutureVoidEmitsLlvmCoroutineIntrinsics) {
    auto llvmIr = emitRobotCoroutineLLVM(R"__(
алг helper
нач
    вверх()
кон
)__");

    // After coro passes the split frame/ramp/resume/destroy are in the IR.
    ASSERT_FALSE(llvmIr.empty());
    EXPECT_NE(llvmIr.find("@robot_up"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@array_create"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@array_destroy"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_future_await_ready"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_future_await_suspend"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("define i32 @__qumir_coro_done"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("define void @__qumir_coro_resume"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("define void @__qumir_coro_destroy"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("AfterCoroEnd"), std::string::npos) << llvmIr;
}

TEST(TypeAnnotation, FutureVoidCoroutineLlvmKeepsLoopControlFlow) {
    auto llvmIr = emitRobotCoroutineLLVM(R"__(
алг helper
нач
    нц пока да
        вверх()
    кц
кон
)__");

    ASSERT_FALSE(llvmIr.empty());
    EXPECT_NE(llvmIr.find("after.await"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("br i1"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_future_await_ready"), std::string::npos) << llvmIr;
}

TEST(TypeAnnotation, FutureVoidCoroutineLlvmAwaitsChildCoroutine) {
    auto llvmIr = emitRobotCoroutineLLVM(R"__(
алг helper
нач
    вверх()
кон

алг caller
нач
    helper()
кон
)__");

    // After coro passes: helper() is called, result wrapped in ITypeErasedFuture*.
    // @llvm.coro.done/resume/destroy remain in the __qumir_coro_* helper bodies.
    ASSERT_FALSE(llvmIr.empty());
    EXPECT_NE(llvmIr.find("call ptr @helper()"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_wrap_coro"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_future_await_ready"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@llvm.coro.done"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@llvm.coro.resume"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@llvm.coro.destroy"), std::string::npos) << llvmIr;
}

TEST(TypeAnnotation, FutureValueCoroutineLlvmUsesPromiseForChildResult) {
    auto llvmIr = emitRobotCoroutineLLVM(R"__(
алг цел caller
нач
    знач := value()
кон

алг цел value
нач
    вверх()
    знач := 12
кон
)__");

    // After coro passes: promise alloca is spilled to coro frame; result loaded.
    ASSERT_FALSE(llvmIr.empty());
    EXPECT_NE(llvmIr.find("coro.promise"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("child.result"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@__qumir_wrap_coro"), std::string::npos) << llvmIr;
}

TEST(TypeAnnotation, OptimizedCoroutineLlvmRunsCoroutinePasses) {
    auto llvmIr = emitRobotCoroutineLLVM(R"__(
алг helper
нач
    вверх()
кон
)__", 1);

    ASSERT_FALSE(llvmIr.empty());
    EXPECT_EQ(llvmIr.find("@llvm.coro.begin"), std::string::npos) << llvmIr;
    EXPECT_EQ(llvmIr.find("@llvm.coro.suspend"), std::string::npos) << llvmIr;
    EXPECT_EQ(llvmIr.find("@llvm.coro.end"), std::string::npos) << llvmIr;
}

TEST(TypeAnnotation, FutureVoidCoroutineLlvmAwaitsExternalActionWithArguments) {
    auto llvmIr = emitTurtleCoroutineLLVM(R"__(
алг helper
нач
    вперед(10.5)
кон
)__");

    ASSERT_FALSE(llvmIr.empty());
    EXPECT_NE(llvmIr.find("@turtle_forward"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("1.050000e+01"), std::string::npos) << llvmIr;
    EXPECT_NE(llvmIr.find("@llvm.coro.suspend"), std::string::npos) << llvmIr;
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
    auto __return = r.Lookup("знач", {6});
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
