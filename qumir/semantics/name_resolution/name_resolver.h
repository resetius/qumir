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

struct TSymbol {
    TSymbolId Id {-1};
    TScopeId ScopeId {-1};
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
    std::unordered_set<int32_t> Symbols;
    std::unordered_map<std::string, TSymbolId> NameToSymbolId;
    bool AllowsRedeclare{false};
    bool RootLevel{false};
};

class TNameResolver {
public:
    TNameResolver(const TNameResolverOptions& options = {});

    std::optional<TError> Resolve(NAst::TExprPtr root);
    TScopePtr GetOrCreateRootScope();

    std::optional<TSymbolId> Lookup(const std::string& name, TScopeId scope) const;
    std::optional<TSymbolId> Lookup(NAst::TExprPtr node) const;
    TSymbolId DeclareFunction(const std::string& name, NAst::TExprPtr node);
    NAst::TExprPtr GetSymbolNode(TSymbolId id) const;

    // For testing/debugging
    const std::vector<TSymbol>& GetSymbols() const {
        return Symbols;
    }

    void PrintSymbols(std::ostream& os) const;

private:
    using TTask = TExpectedTask<std::monostate, TError, TLocation>;
    TTask Resolve(NAst::TExprPtr node, TScopePtr scope);
    TTask Declare(const std::string& name, TScopePtr scope, NAst::TExprPtr node);
    TScopePtr NewScope(TScopePtr parent);

    TNameResolverOptions Options;
    std::vector<TSymbol> Symbols;
    std::unordered_map<NAst::TExprPtr, TSymbolId> NodeToSymbolId;

    std::vector<TScopePtr> Scopes;
};

} // namespace NSemantics
} // namespace NQumir
