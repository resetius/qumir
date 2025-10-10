#include "name_resolver.h"

#include <iostream>

namespace NQumir {
namespace NSemantics {

using namespace NAst;

TNameResolver::TNameResolver(const TNameResolverOptions& options)
    : Options(options)
{ }

// Function declaration may be at the top level only
// They can be in any order, so we need to do two passes
TNameResolver::TTask TNameResolver::ResolveTopFuncDecl(NAst::TExprPtr node, TScopePtr scope)
{
    auto declFunc = [&](NAst::TExprPtr node) -> TTask {
        if (auto maybeFdecl = TMaybeNode<TFunDecl>(node)) {
            auto fdecl = maybeFdecl.Cast();
            if (fdecl->Name.empty()) {
                co_return TError(fdecl->Location, "function with empty name");
            }
            co_await Declare(fdecl->Name, node, scope, nullptr);
            auto newScope = NewScope(scope, nullptr);
            fdecl->Scope = newScope->Id.Id;
            for (auto& param : fdecl->Params) {
                co_await Declare(param->Name, param, newScope, newScope);
            }
            // resolve body on second pass
        }
        co_return std::monostate{};
    };

    auto maybeBlock = TMaybeNode<TBlockExpr>(node);
    if (maybeBlock) {
        auto block = maybeBlock.Cast();
        for (const auto& stmt : block->Stmts) {
            co_await declFunc(stmt);
        }
    }

    co_return {};
}

TNameResolver::TTask TNameResolver::Resolve(TExprPtr node, TScopePtr scope, TScopePtr funcScope) {
    if (auto maybeFdecl = TMaybeNode<TFunDecl>(node)) {
        auto fdecl = maybeFdecl.Cast();
        auto scopeId = TScopeId{fdecl->Scope};
        if (scopeId.Id < 0 || static_cast<size_t>(scopeId.Id) >= Scopes.size())  {
            co_return TError(fdecl->Location, "function has invalid scope id: " + std::to_string(scopeId.Id));
        }
        auto newScope = Scopes[scopeId.Id];
        co_return co_await Resolve(fdecl->Body, newScope, newScope);
    } else if (auto maybeBlock = TMaybeNode<TBlockExpr>(node)) {
        auto block = maybeBlock.Cast();
        if (block->Scope < 0) {
            scope = NewScope(scope, funcScope);
            block->Scope = scope->Id.Id;
        }
    } else if (auto maybeIdent = TMaybeNode<TIdentExpr>(node)) {
        auto ident = maybeIdent.Cast();
        auto found = Lookup(ident->Name, scope->Id);
        if (!found) {
            co_return TError(ident->Location, "undefined identifier: " + ident->Name + " in scope " + std::to_string(scope->Id.Id));
        }
        co_return {};
    } else if (auto maybeAsg = TMaybeNode<TAssignExpr>(node)) {
        auto asg = maybeAsg.Cast();
        auto found = Lookup(asg->Name, scope->Id);
        if (!found) {
            co_return TError(asg->Location, "assignment to undefined identifier: " + asg->Name + " in scope " + std::to_string(scope->Id.Id));
        }
    } else if (auto maybeVarStmt = TMaybeNode<TVarStmt>(node)) {
        auto varStmt = maybeVarStmt.Cast();
        auto res = Declare(varStmt->Name, node, scope, funcScope);
        if (!res) {
            co_return TError(varStmt->Location, res.error().what());
        }
        co_return {};
    }

    for (const auto& child : node->Children()) {
        if (child == nullptr) continue; // LoopExpr may have null children
        co_await Resolve(child, scope, funcScope);
    }

    co_return {};
}

std::optional<TError> TNameResolver::Resolve(TExprPtr root) {
    auto scope = GetOrCreateRootScope();
    if (auto block = TMaybeNode<TBlockExpr>(root)) {
        block.Cast()->Scope = scope->Id.Id;
    }
    auto funcRes = ResolveTopFuncDecl(root, scope).result();
    if (!funcRes) {
        return funcRes.error();
    }
    auto res = Resolve(root, scope, nullptr).result();
    if (!res) {
        return res.error();
    }
    return {};
}

std::optional<TSymbolInfo> TNameResolver::Lookup(const std::string& name, TScopeId scopeId) const {
    TScopePtr scope = nullptr;
    if (scopeId.Id < 0 || static_cast<size_t>(scopeId.Id) >= Scopes.size()) {
        return std::nullopt;
    }
    scope = Scopes[scopeId.Id];
    while (scope) {
        auto it = scope->NameToSymbolId.find(name);
        if (it != scope->NameToSymbolId.end()) {
            auto& symbol = Symbols[it->second.Id];
            return TSymbolInfo {
                .Id = symbol.Id.Id,
                .DeclScopeId = symbol.ScopeId.Id,
                .ScopeLevelIdx = symbol.ScopeLevelIdx,
                .FunctionLevelIdx = symbol.FunctionLevelIdx,
                .FuncScopeId = symbol.FuncScopeId.Id,
            };
        }
        scope = scope->Parent;
    }
    return std::nullopt;
}

std::expected<TSymbolId, TError> TNameResolver::Declare(const std::string& name, TExprPtr node, TScopePtr scope, TScopePtr funcScope) {
    auto maybeSymbolId = scope->NameToSymbolId.find(name);
    TSymbolId symbolId{-1};
    if (maybeSymbolId != scope->NameToSymbolId.end()) {
        if (!scope->AllowsRedeclare) {
            auto& symbol = Symbols[maybeSymbolId->second.Id];
            std::ostringstream ss;
            ss << "Переопределение `" << symbol.Name << "' уже объявлено в области видимости " << symbol.ScopeId.Id;
            return std::unexpected(TError(node->Location, ss.str()));
        }
        symbolId = maybeSymbolId->second;
    }

    TSymbol& symbol = symbolId.Id != -1
        ? Symbols[symbolId.Id] // redeclare
        : Symbols.emplace_back(TSymbol {
            .Id = {(int32_t)Symbols.size()},
            .ScopeId = scope->Id,
            .ScopeLevelIdx = (int32_t)scope->Symbols.size(),
            .FunctionLevelIdx = funcScope ? (int32_t)funcScope->FuncSymbols.size() : -1,
            .FuncScopeId = funcScope ? funcScope->Id : TScopeId{-1},
            .Name = name,
            .Node = node,
        });

    if (funcScope) {
        funcScope->FuncSymbols.insert(symbol.Id.Id);
    }
    scope->Symbols.emplace(symbol.Id.Id);
    scope->NameToSymbolId[name] = symbol.Id;

    return symbol.Id;
}

TSymbolId TNameResolver::DeclareFunction(const std::string& name, TExprPtr node) {
    auto scope = GetOrCreateRootScope();
    auto res = Declare(name, node, scope, nullptr);
    if (!res) {
        throw std::runtime_error(std::string("failed to declare function: ") + res.error().what());
    }

    return res.value();
}

std::vector<std::pair<int, std::shared_ptr<NAst::TFunDecl>>> TNameResolver::GetExternalFunctions() {
    auto scope = GetOrCreateRootScope();

    std::vector<std::pair<int, std::shared_ptr<NAst::TFunDecl>>> result;
    for (auto& symbolId : scope->Symbols) {
        auto& symbol = Symbols[symbolId];
        if (auto maybeFun = TMaybeNode<TFunDecl>(symbol.Node)) {
            auto fun = maybeFun.Cast();
            if (fun->Body == nullptr) {
                result.push_back({symbolId, fun});
            }
        }
    }
    return result;
}

TScopePtr TNameResolver::NewScope(TScopePtr parent, TScopePtr funcScope) {
    auto newScope = std::make_shared<TScope>(TScope {
        .Id = {(int32_t)Scopes.size()},
        .Parent = parent,
        .FuncScope = funcScope,
    });
    Scopes.push_back(newScope);
    return newScope;
}

TScopePtr TNameResolver::GetOrCreateRootScope() {
    if (Scopes.empty()) {
        auto root = NewScope(nullptr, nullptr);
        root->RootLevel = true;
        return root;
    }
    return Scopes[0];
}

TExprPtr TNameResolver::GetSymbolNode(TSymbolId id) const {
    if (id.Id < 0 || static_cast<size_t>(id.Id) >= Symbols.size()) {
        return nullptr;
    }
    return Symbols[id.Id].Node;
}

void TNameResolver::PrintSymbols(std::ostream& os) const {
    for (const auto& symbol : Symbols) {
        os << "Symbol: " << symbol.Name << ", Scope: " << symbol.ScopeId.Id << "\n";
    }
}

} // namespace NSemantics
} // namespace NQumir