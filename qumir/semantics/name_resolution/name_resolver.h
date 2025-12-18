#pragma once

#include <qumir/parser/ast.h>
#include <qumir/location.h>
#include <qumir/error.h>
#include <qumir/optional.h>

#include <span>
#include <unordered_map>
#include <unordered_set>

namespace NQumir {

namespace NRegistry {

class IModule;

} // namespace NRegistry

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
    std::vector<uint32_t> CodePoints;
    NAst::TExprPtr Node;
};

struct TSuggestion {
    std::string OriginalName;
    std::string Name;
    std::optional<std::string> RequiredModuleName;
    int Distance;

    std::string ToString() const {
        if (Distance == 0 && RequiredModuleName) {
            return "\n Возможно вы забыли импортировать модуль `" + *RequiredModuleName + "',\n добавьте строку `использовать " + *RequiredModuleName + "' в начало программы.";
        }
        std::string result = "\n Возможно вы имели в виду `" + Name + "'";;
        if (RequiredModuleName) {
            result += " из модуля `" + *RequiredModuleName + "',\n добавьте строку `использовать " + *RequiredModuleName + "' в начало программы и замените `" + OriginalName + "' на `" + Name + "'.";
        }
        return result;
    }
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

class TEditDistance {
public:
    template<typename T>
    int Calc(std::span<const T> a, std::span<const T> b) {
        int n = a.size();
        int m = b.size();
        DP.resize((n + 1) * (m + 1), 0);

        for (int i = 0; i <= n; i++) {
            DP[i * (m + 1) + 0] = i;
        }
        for (int j = 0; j <= m; j++) {
            DP[0 * (m + 1) + j] = j;
        }

        for (int i = 1; i <= n; i++) {
            for (int j = 1; j <= m; j++) {
                int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
                DP[i * (m + 1) + j] = std::min({
                    DP[(i - 1) * (m + 1) + j] + 1,      // deletion
                    DP[i * (m + 1) + (j - 1)] + 1,      // insertion
                    DP[(i - 1) * (m + 1) + (j - 1)] + cost // substitution
                });
            }
        }

        return DP[n * (m + 1) + m];
    }

private:
    std::vector<int> DP;
};

class TNameResolver {
public:
    TNameResolver(const TNameResolverOptions& options = {});

    std::optional<TError> Resolve(NAst::TExprPtr root);
    TScopePtr GetOrCreateRootScope();

    std::optional<TSymbolInfo> Lookup(const std::string& name, TScopeId scope) const;
    std::optional<TSuggestion> Suggest(const std::string& name, TScopeId scope, bool includeFunctions);

    TSymbolId DeclareFunction(const std::string& name, NAst::TExprPtr node);
    NAst::TExprPtr GetSymbolNode(TSymbolId id) const;
    std::vector<std::pair<int, std::shared_ptr<NAst::TFunDecl>>> GetExternalFunctions();

    TSymbolId Declare(const std::string& name, NAst::TExprPtr node, TSymbolInfo parentSymbol);

    // just adds module to dict of modules
    void RegisterModule(NRegistry::IModule* module);
    // adds modules symbols to list of symbols
    bool ImportModule(const std::string& name);
    std::string ModulesList() const;

    // For testing/debugging
    const std::vector<TSymbol>& GetSymbols() const {
        return Symbols;
    }

    std::vector<TSymbolInfo> GetGlobals() const {
        if (Scopes.empty()) {
            return {};
        }
        auto& rootScope = Scopes[0];
        std::vector<TSymbolInfo> result;
        for (auto& symbolId : rootScope->Symbols) {
            auto& symbol = Symbols[symbolId];
            //std::cerr << "Global symbol: " << symbol.Name << " " << symbol.Id.Id << "\n";
            result.push_back(TSymbolInfo {
                .Id = symbol.Id.Id,
                .DeclScopeId = symbol.ScopeId.Id,
                .ScopeLevelIdx = symbol.ScopeLevelIdx,
                .FunctionLevelIdx = symbol.FunctionLevelIdx,
                .FuncScopeId = symbol.FuncScopeId.Id,
            });
        }
        return result;
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

    std::unordered_map<std::string, NRegistry::IModule*> Modules;

    TEditDistance EditDistanceCalculator;
};

} // namespace NSemantics
} // namespace NQumir
