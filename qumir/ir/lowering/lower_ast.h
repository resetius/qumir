#pragma once

#include <qumir/ir/builder.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <qumir/optional.h>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

class TAstLowerer {
public:
    TAstLowerer(TModule& module, TBuilder& builder, NSemantics::TNameResolver& ctx)
        : Module(module), Builder(builder), Context(ctx)
    {}

    std::expected<std::monostate, TError> LowerTop(const NAst::TExprPtr& expr);

private:
    struct TBlockScope {
        int64_t FuncIdx;
        NSemantics::TScopeId Id;
        std::optional<TLabel> BreakLabel;
        std::optional<TLabel> ContinueLabel;
    };

    struct TDestructor {
        std::vector<TOperand> Args;
        std::vector<int> TypeIds;
        TImm FunctionId;
    };

    //struct TPendingDestructors {
    //    std::vector<NSemantics::TSymbolInfo> Strings;
    //};
    using TPendingDestructors = std::vector<TDestructor>;

    enum class EOwnership {
        Unkwnown,
        Owned,
        Borrowed
    };

    struct TValueWithBlock {
        std::optional<TOperand> Value; // absent => no value
        TLabel ProducingLabel; // label of block that produced Value (or current block if no value)
        EOwnership Ownership = EOwnership::Unkwnown; // heap object ownership (strings), used for destructor calls
    };

    TExpectedTask<TValueWithBlock, TError, TLocation> Lower(const NAst::TExprPtr& expr, TBlockScope scope);

    TExpectedTask<TValueWithBlock, TError, TLocation> LowerLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerWhileLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerForLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerRepeatLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerIndices(const std::string& name, const std::vector<NAst::TExprPtr>& indices, TBlockScope scope);
    TExpectedTask<TTmp, TError, TLocation> LoadVar(const std::string& name, TBlockScope scope, const TLocation& loc);

    void ImportExternalFunction(int symbolId, const NAst::TFunDecl& funcDecl);
    void ImportExternalFunctions();
    TExpectedTask<int, TError, TLocation> GlobalSymbolId(const std::string& name);

    TModule& Module;
    TBuilder& Builder;
    NSemantics::TNameResolver& Context;

    TPendingDestructors PendingDestructors;

    int64_t NextReplChunk = 0;
    int64_t NextLambdaChunk = 0;
};

} // namespace NIR
} // namespace NQumir
