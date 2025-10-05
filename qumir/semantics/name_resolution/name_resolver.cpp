#include "name_resolver.h"

#include <iostream>

namespace NQumir {
namespace NSemantics {

using namespace NAst;

TNameResolver::TNameResolver(const TNameResolverOptions& options)
    : Options(options)
{ }

TNameResolver::TTask TNameResolver::Resolve(TExprPtr node, TScopePtr scope) {
    if (auto maybeFdecl = TMaybeNode<TFunDecl>(node)) {
        auto fdecl = maybeFdecl.Cast();
        if (fdecl->Name.empty()) {
            co_return TError(fdecl->Location, "function with empty name");
        }
        co_await Declare(fdecl->Name, scope, node);
        auto newScope = NewScope(scope);
        for (auto& param : fdecl->Params) {
            co_await Declare(param->Name, newScope, param);
        }
        co_return co_await Resolve(fdecl->Body, newScope);
    } else if (auto maybeBlock = TMaybeNode<TBlockExpr>(node)) {
        auto block = maybeBlock.Cast();
        if (block->Scope < 0) {
            scope = NewScope(scope);
            block->Scope = scope->Id.Id;
        }
    } else if (auto maybeIdent = TMaybeNode<TIdentExpr>(node)) {
        auto ident = maybeIdent.Cast();
        auto found = Lookup(ident->Name, scope->Id);
        if (!found) {
            co_return TError(ident->Location, "undefined identifier: " + ident->Name + " in scope " + std::to_string(scope->Id.Id));
        }
        NodeToSymbolId[ident] = *found;
        co_return {};
    } else if (auto maybeAsg = TMaybeNode<TAssignExpr>(node)) {
        auto asg = maybeAsg.Cast();
        auto found = Lookup(asg->Name, scope->Id);
        if (!found) {
            co_return TError(asg->Location, "assignment to undefined identifier: " + asg->Name + " in scope " + std::to_string(scope->Id.Id));
        }
        NodeToSymbolId[asg] = *found;
    } else if (auto maybeVarStmt = TMaybeNode<TVarStmt>(node)) {
        auto varStmt = maybeVarStmt.Cast();
        co_await Declare(varStmt->Name, scope, node);
    }

    for (const auto& child : node->Children()) {
        co_await Resolve(child, scope);
    }

    co_return {};
}

std::optional<TError> TNameResolver::Resolve(TExprPtr root) {
    auto scope = GetOrCreateRootScope();
    if (auto block = TMaybeNode<TBlockExpr>(root)) {
        block.Cast()->Scope = scope->Id.Id;
    }
    auto res = Resolve(root, scope).result();
    if (!res) {
        return res.error();
    }
    return {};
}

std::optional<TSymbolId> TNameResolver::Lookup(const std::string& name, TScopeId scopeId) const {
    TScopePtr scope = nullptr;
    if (scopeId.Id < 0 || static_cast<size_t>(scopeId.Id) >= Scopes.size()) {
        return std::nullopt;
    }
    scope = Scopes[scopeId.Id];
    while (scope) {
        auto it = scope->NameToSymbolId.find(name);
        if (it != scope->NameToSymbolId.end()) {
            return it->second;
        }
        scope = scope->Parent;
    }
    return std::nullopt;
}

std::optional<TSymbolId> TNameResolver::Lookup(TExprPtr node) const
{
    auto it = NodeToSymbolId.find(node);
    if (it == NodeToSymbolId.end()) {
        return std::nullopt;
    }
    return it->second;
}

TNameResolver::TTask TNameResolver::Declare(const std::string& name, TScopePtr scope, TExprPtr node) {
    auto maybeSymbolId = scope->NameToSymbolId.find(name);
    TSymbolId symbolId{-1};
    if (maybeSymbolId != scope->NameToSymbolId.end()) {
        if (!scope->AllowsRedeclare) {
            auto& symbol = Symbols[maybeSymbolId->second.Id];
            std::ostringstream ss;
            ss << "Переопределение `" << symbol.Name << "' уже объявлено в области видимости " << symbol.ScopeId.Id;
            co_return TError(node->Location, ss.str());
        }
        symbolId = maybeSymbolId->second;
    }

    TSymbol& symbol = symbolId.Id != -1
        ? Symbols[symbolId.Id] // redeclare
        : Symbols.emplace_back(TSymbol {
            .Id = {(int32_t)Symbols.size()},
            .ScopeId = scope->Id,
            .Name = name,
            .Node = node,
        });

    scope->Symbols.emplace(symbol.Id.Id);
    scope->NameToSymbolId[name] = symbol.Id;

    NodeToSymbolId[node] = symbol.Id;
    co_return {};
}

TSymbolId TNameResolver::DeclareFunction(const std::string& name, TExprPtr node) {
    auto scope = GetOrCreateRootScope();
    auto res = Declare(name, scope, node).result();
    if (!res) {
        throw std::runtime_error(std::string("failed to declare function: ") + res.error().what());
    }
    auto it = NodeToSymbolId.find(node);
    if (it == NodeToSymbolId.end()) {
        throw std::runtime_error("failed to find declared function symbol");
    }
    return it->second;
}

TScopePtr TNameResolver::NewScope(TScopePtr parent) {
    auto newScope = std::make_shared<TScope>(TScope {
        .Id = {(int32_t)Scopes.size()},
        .Parent = parent,
    });
    Scopes.push_back(newScope);
    return newScope;
}

TScopePtr TNameResolver::GetOrCreateRootScope() {
    if (Scopes.empty()) {
        auto root = NewScope(nullptr);
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