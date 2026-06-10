#pragma once

#include <qumir/ir/builder.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>

#include <qumir/optional.h>

#include <unordered_map>

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
        std::optional<TLabel> ReturnLabel; // function's exit block, set for every scope inside a function body
        std::optional<TLocal> RetLocal; // holds the return value; unset for void-returning functions
    };

    struct TDestructor {
        std::vector<TOperand> Args;
        std::vector<int> TypeIds;
        TImm FunctionId;
    };

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

    struct TArrayLayout {
        std::vector<TOperand> LBounds;
        std::vector<TOperand> DimSizes;
        std::vector<TOperand> Strides;
        TOperand TotalElements;
    };

    TExpectedTask<TValueWithBlock, TError, TLocation> Lower(const NAst::TExprPtr& expr, TBlockScope scope);

    // Lowers `ret->Value` (if any) to an operand suitable for storing into
    // `scope.RetLocal`, retaining it if it's a borrowed string. Caller is
    // responsible for emitting the `stre`/`jmp` and any destructor calls.
    TExpectedTask<std::optional<TOperand>, TError, TLocation> LowerReturnValue(std::shared_ptr<NAst::TReturnExpr> ret, TBlockScope scope);

    TExpectedTask<TValueWithBlock, TError, TLocation> LowerWhile(std::shared_ptr<NAst::TWhileStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerFor(std::shared_ptr<NAst::TForStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerRepeat(std::shared_ptr<NAst::TRepeatStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerTimes(std::shared_ptr<NAst::TTimesStmtExpr> loop, TBlockScope scope);
    TExpectedTask<TArrayLayout, TError, TLocation> LowerArrayLayout(NSemantics::TSymbolInfo symbol, const std::vector<std::pair<NAst::TExprPtr, NAst::TExprPtr>>& bounds, TBlockScope scope, const TLocation& loc);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerIndices(NSemantics::TSymbolInfo symbol, const std::vector<NAst::TExprPtr>& indices, TBlockScope scope, int elemSize = 8);
    TExpectedTask<TValueWithBlock, TError, TLocation> LowerLValueAddress(const NAst::TExprPtr& expr, TBlockScope scope);
    TExpectedTask<TTmp, TError, TLocation> LoadVar(const std::string& name, TBlockScope scope, const TLocation& loc, bool ref = false);
    TTmp LoadLayoutOperand(TOperand operand);
    TOperand AllocLayoutStorage(NSemantics::TSymbolInfo symbol, int typeId);

    void ImportExternalFunction(int symbolId, const NAst::TFunDecl& funcDecl);
    void ImportExternalFunctions();
    TExpectedTask<int, TError, TLocation> GlobalSymbolId(const std::string& name);

    TModule& Module;
    TBuilder& Builder;
    NSemantics::TNameResolver& Context;

    TPendingDestructors PendingDestructors;
    std::unordered_map<int32_t, TArrayLayout> ArrayLayouts;
    int32_t NextHiddenGlobalSlot = -1;

    int64_t NextReplChunk = 0;
    int64_t NextLambdaChunk = 0;
};

} // namespace NIR
} // namespace NQumir
