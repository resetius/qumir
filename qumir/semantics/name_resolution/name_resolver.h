#pragma once

#include <qumir/parser/ast.h>
#include <qumir/location.h>
#include <qumir/error.h>
#include <qumir/optional.h>

#include <unordered_map>
#include <unordered_set>

namespace NQumir {
namespace NSemantics {

struct TScopeId {
    int32_t Id;
};

struct TSymbolId {
    int32_t Id;
};

struct TSymbolInfo {
    int32_t Id;
    int32_t DeclScopeId;
    int32_t ScopeLevelIdx;
    int32_t FunctionLevelIdx;
    int32_t FuncScopeId;
};

struct TSymbol {
    TSymbolId Id {-1};
    TScopeId ScopeId {-1};
    int32_t ScopeLevelIdx {-1};
    int32_t FunctionLevelIdx {-1}; // index among function-local symbols, -1 if not in function scope
    TScopeId FuncScopeId {-1}; // function scope id, -1 if not in function scope
    std::string Name;
    NAst::TExprPtr Node;
};

struct TNameResolverOptions {
};

struct TScope;
using TScopePtr = std::shared_ptr<TScope>;

struct TScope {
    TScopeId Id;
    TScopePtr Parent;
    TScopePtr FuncScope;
    std::unordered_set<int32_t> Symbols;
    std::unordered_set<int32_t> FuncSymbols; // if scope is function scope - all local symbols of function
    std::unordered_map<std::string, TSymbolId> NameToSymbolId;
    bool AllowsRedeclare{false};
    bool RootLevel{false};
};

class TNameResolver {
public:
    TNameResolver(const TNameResolverOptions& options = {});

    std::optional<TError> Resolve(NAst::TExprPtr root);
    TScopePtr GetOrCreateRootScope();

    std::optional<TSymbolInfo> Lookup(const std::string& name, TScopeId scope) const;
    TSymbolId DeclareFunction(const std::string& name, NAst::TExprPtr node);
    NAst::TExprPtr GetSymbolNode(TSymbolId id) const;
    std::vector<std::pair<int, std::shared_ptr<NAst::TFunDecl>>> GetExternalFunctions();

    // For testing/debugging
    const std::vector<TSymbol>& GetSymbols() const {
        return Symbols;
    }

    void PrintSymbols(std::ostream& os) const;

private:
    using TTask = TExpectedTask<std::monostate, TError, TLocation>;
    TTask Resolve(NAst::TExprPtr node, TScopePtr parentScope, TScopePtr funcScope);
    TTask ResolveTopFuncDecl(NAst::TExprPtr node, TScopePtr scope);
    std::expected<TSymbolId, TError> Declare(const std::string& name, NAst::TExprPtr node, TScopePtr scope, TScopePtr funcScope);
    TScopePtr NewScope(TScopePtr parent, TScopePtr funcScope);

    TNameResolverOptions Options;
    std::vector<TSymbol> Symbols;

    std::vector<TScopePtr> Scopes;
};

} // namespace NSemantics
} // namespace NQumir
