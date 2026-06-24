#include "name_resolver.h"

#include <qumir/modules/module.h>
#include <qumir/parser/type.h>

#include <iostream>

namespace NQumir {
namespace NSemantics {

using namespace NAst;

TNameResolver::TNameResolver(const TNameResolverOptions& options)
    : Options(options)
{ }

void TNameResolver::ApplyPragmas(const std::vector<NAst::TPragma>& pragmas) {
    for (const auto& pragma : pragmas) {
        if (pragma.Group == "language") {
            for (const auto& value : pragma.Values) {
                if (value == "overloads") {
                    Options.AllowOverloads = true;
                }
            }
        }
    }
}

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
            if (fdecl->Scope >= 0) {
                if (static_cast<size_t>(fdecl->Scope) >= Scopes.size()) {
                    co_return TError(fdecl->Location, "function has invalid scope id: " + std::to_string(fdecl->Scope));
                }
                co_return std::monostate{};
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
    std::list<TError> errors;

    auto suggestionMessage = [&](const std::string& name, int scopeId, bool includeFunctions) {
        auto suggestion = Suggest(name, TScopeId{scopeId}, includeFunctions);
        return suggestion ? suggestion->ToString() : "";
    };

    // Resolve TNamedType: fill in UnderlyingType from imported module types.
    // Also recurses into TReferenceType and TPointerType wrappers.
    auto resolveTypeRef = [&](auto& self, NAst::TTypePtr& type, const TLocation& loc) -> std::optional<TError> {
        if (!type) return {};
        if (auto maybeNamed = TMaybeType<TNamedType>(type)) {
            auto named = maybeNamed.Cast();
            if (named->Template) {
                // Generic type parameter placeholder ("<named K (template)>"):
                // intentionally left unresolved here — the concrete type is
                // bound per call site and the function gets instantiated for
                // it. Treating it as an unknown type would make it impossible
                // to declare generic functions at all.
                return {};
            }
            auto it = ImportedTypes.find(named->Name);
            if (it == ImportedTypes.end()) {
                return TError(loc, "Неизвестный тип: " + named->Name);
            }
            named->UnderlyingType = it->second;
            auto modIt = ImportedModuleSymbols.find(named->Name);
            if (modIt != ImportedModuleSymbols.end()) {
                named->Reference = modIt->second; // marks type as imported from this module
            }
            return self(self, named->UnderlyingType, loc);
        } else if (auto maybeRef = TMaybeType<TReferenceType>(type)) {
            return self(self, maybeRef.Cast()->ReferencedType, loc);
        } else if (auto maybePtr = TMaybeType<TPointerType>(type)) {
            return self(self, maybePtr.Cast()->PointeeType, loc);
        } else if (auto maybeArray = TMaybeType<TArrayType>(type)) {
            return self(self, maybeArray.Cast()->ElementType, loc);
        } else if (auto maybeFun = TMaybeType<TFunctionType>(type)) {
            auto fun = maybeFun.Cast();
            if (auto err = self(self, fun->ReturnType, loc)) {
                return err;
            }
            for (auto& param : fun->ParamTypes) {
                if (auto err = self(self, param, loc)) {
                    return err;
                }
            }
        } else if (auto maybeFuture = TMaybeType<TFutureType>(type)) {
            return self(self, maybeFuture.Cast()->ResultType, loc);
        } else if (auto maybeStruct = TMaybeType<TStructType>(type)) {
            for (auto& [_, fieldType] : maybeStruct.Cast()->Fields) {
                if (auto err = self(self, fieldType, loc)) {
                    return err;
                }
            }
        }
        return {};
    };
    auto resolveTypeRefOuter = [&](NAst::TTypePtr& type, const TLocation& loc) -> std::optional<TError> {
        return resolveTypeRef(resolveTypeRef, type, loc);
    };

    // TODO: resolveTypeRefOuter on every node is O(n*depth); consider resolving types
    // only at declaration sites (TVarStmt, TFunDecl params) rather than all nodes.
    if (node->Type) {
        if (auto err = resolveTypeRefOuter(node->Type, node->Location)) {
            co_return *err;
        }
    }

    if (auto maybeFdecl = TMaybeNode<TFunDecl>(node)) {
        auto fdecl = maybeFdecl.Cast();
        auto scopeId = TScopeId{fdecl->Scope};
        if (scopeId.Id < 0 || static_cast<size_t>(scopeId.Id) >= Scopes.size())  {
            co_return TError(fdecl->Location, "function has invalid scope id: " + std::to_string(scopeId.Id));
        }
        if (auto err = resolveTypeRefOuter(fdecl->RetType, fdecl->Location)) {
            co_return *err;
        }
        for (auto& param : fdecl->Params) {
            if (auto err = resolveTypeRefOuter(param->Type, param->Location)) {
                co_return *err;
            }
        }
        auto newScope = Scopes[scopeId.Id];
        co_await Resolve(fdecl->Body, newScope, newScope);
        if (fdecl->LastAssert) {
            auto bodyScope = Scopes[fdecl->Body->Scope];
            co_await Resolve(fdecl->LastAssert, bodyScope, newScope);
        }
        co_return std::monostate{};
    } else if (auto maybeBlock = TMaybeNode<TBlockExpr>(node)) {
        auto block = maybeBlock.Cast();
        if (block->Scope < 0) {
            scope = NewScope(scope, funcScope);
            block->Scope = scope->Id.Id;
        } else {
            if (static_cast<size_t>(block->Scope) >= Scopes.size()) {
                co_return TError(block->Location, "block has invalid scope id: " + std::to_string(block->Scope));
            }
            scope = Scopes[block->Scope];
        }
    } else if (auto maybeIdent = TMaybeNode<TIdentExpr>(node)) {
        auto ident = maybeIdent.Cast();
        auto found = Lookup(ident->Name, scope->Id);
        if (!found) {
            if (!LookupOverloads(ident->Name, scope->Id).empty()) {
                co_return {};
            }
            auto suggestionMsg = suggestionMessage(ident->Name, scope->Id.Id, /*includeFunctions=*/ true);
            co_return TError(ident->Location, TErrorString::Get<EErrorId::UNDEFINED_IDENTIFIER>(ident->Name) + suggestionMsg);
        }

        co_return {};
    } else if (auto maybeAsg = TMaybeNode<TAssignExpr>(node)) {
        auto asg = maybeAsg.Cast();
        auto found = Lookup(asg->Name, scope->Id);
        if (!found) {
            auto suggestionMsg = suggestionMessage(asg->Name, scope->Id.Id, /*includeFunctions=*/ false);
            errors.emplace_back(TError(asg->Location, TErrorString::Get<EErrorId::UNDEFINED_IDENTIFIER>(asg->Name) + suggestionMsg));
        }
    } else if (auto maybeFor = TMaybeNode<TForStmtExpr>(node)) {
        auto forExpr = maybeFor.Cast();
        auto found = Lookup(forExpr->VarName, scope->Id);
        if (!found) {
            auto suggestionMsg = suggestionMessage(forExpr->VarName, scope->Id.Id, /*includeFunctions=*/ false);
            errors.emplace_back(TError(forExpr->Location, TErrorString::Get<EErrorId::UNDEFINED_IDENTIFIER>(forExpr->VarName) + suggestionMsg));
        }
    } else if (auto maybeVarStmt = TMaybeNode<TVarStmt>(node)) {
        auto varStmt = maybeVarStmt.Cast();
        if (auto err = resolveTypeRefOuter(varStmt->Type, varStmt->Location)) {
            co_return *err;
        }
        auto existing = scope->NameToSymbolId.find(varStmt->Name);
        if (existing != scope->NameToSymbolId.end()
            && GetSymbolNode(existing->second) == node)
        {
            if (varStmt->Init) {
                auto res = Resolve(varStmt->Init, scope, funcScope).result();
                if (!res) { co_return res.error(); }
            }
            co_return {};
        }
        auto res = Declare(varStmt->Name, node, scope, funcScope);
        if (!res) {
            co_return res.error();
        }
        if (varStmt->Init) {
            auto initRes = Resolve(varStmt->Init, scope, funcScope).result();
            if (!initRes) { co_return initRes.error(); }
        }
        co_return {};
    } else if (auto maybeTypeDecl = TMaybeNode<TTypeDeclStmt>(node)) {
        if (auto maybeNamed = TMaybeType<TNamedType>(maybeTypeDecl.Cast()->Type)) {
            auto named = maybeNamed.Cast();
            RegisterType(named->Name, named->UnderlyingType);
        }
        co_return {};
    }

    for (const auto& child : node->Children()) {
        if (child == nullptr) continue; // LoopExpr may have null children
        auto res = Resolve(child, scope, funcScope).result();
        if (!res) {
            errors.emplace_back(res.error());
        }
    }

    if (errors.empty()) {
        co_return {};
    }
    co_return TError(node->Location, errors);
}

std::optional<TError> TNameResolver::Resolve(TExprPtr root) {
    auto scope = GetOrCreateRootScope();
    if (auto block = TMaybeNode<TBlockExpr>(root)) {
        block.Cast()->Scope = scope->Id.Id;
    }
    ImportUseStmts(root);
    if (auto err = RegisterTypeDecls(root)) {
        return err;
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

std::optional<TSuggestion> TNameResolver::Suggest(const std::string& name, TScopeId scopeId, bool includeFunctions) {
    TScopePtr scope = nullptr;
    if (scopeId.Id < 0 || static_cast<size_t>(scopeId.Id) >= Scopes.size()) {
        return std::nullopt;
    }
    scope = Scopes[scopeId.Id];

    static constexpr int MAX_DISTANCE = 2;
    TSuggestion bestSuggestion;
    bestSuggestion.Distance = INT32_MAX;
    bestSuggestion.OriginalName = name;
    std::unordered_set<std::string> checkedNames;

    auto calcCodePoint = [](const std::string& s) {
        // parse utf-8 to code points
        std::vector<uint32_t>  cp;
        uint32_t codepoint = 0;
        for (char c : s) {
            if ((c & 0x80) == 0) {
                // 1-byte
                if (codepoint != 0) {
                    cp.push_back(codepoint);
                    codepoint = 0;
                }
                cp.push_back(c);
            } else if ((c & 0xC0) == 0x80) {
                // continuation byte
                codepoint = (codepoint << 6) | (c & 0x3F);
            } else if ((c & 0xE0) == 0xC0) {
                // 2-byte
                if (codepoint != 0) {
                    cp.push_back(codepoint);
                }
                codepoint = c & 0x1F;
            } else if ((c & 0xF0) == 0xE0) {
                // 3-byte
                if (codepoint != 0) {
                    cp.push_back(codepoint);
                }
                codepoint = c & 0x0F;
            } else if ((c & 0xF8) == 0xF0) {
                // 4-byte
                if (codepoint != 0) {
                    cp.push_back(codepoint);
                }
                codepoint = c & 0x07;
            }
        }
        return cp;
    };

    auto nameCodePoints = calcCodePoint(name);

    while (scope) {
        for (auto symbolId : scope->Symbols) {
            auto& symbol = Symbols[symbolId];
            if (!includeFunctions) {
                if (TMaybeNode<TFunDecl>(symbol.Node)) {
                    continue;
                }
            }
            if (checkedNames.find(symbol.Name) != checkedNames.end()) {
                continue;
            }
            checkedNames.insert(symbol.Name);
            if (symbol.CodePoints.empty()) {
                symbol.CodePoints = calcCodePoint(symbol.Name);
            }
            if ( (int)symbol.CodePoints.size() - (int)nameCodePoints.size() > MAX_DISTANCE ) {
                continue;
            }
            int distance = EditDistanceCalculator.Calc(
                std::span<const uint32_t>(nameCodePoints.data(), nameCodePoints.size()),
                std::span<const uint32_t>(symbol.CodePoints.data(), symbol.CodePoints.size())
            );
            if (distance < bestSuggestion.Distance && distance <= MAX_DISTANCE) {
                bestSuggestion.Name = symbol.Name;
                bestSuggestion.Distance = distance;
            }
        }
        scope = scope->Parent;
    }

    // search in modules
    if (includeFunctions && bestSuggestion.Distance == INT32_MAX) {
        for (const auto& [moduleName, module] : Modules) {
            if (!module) {
                continue; // skip null module pointer
            }
            for (const auto& extFunc : module->ExternalFunctions()) {
                const auto& symbolName = extFunc.Name;
                if (checkedNames.find(symbolName) != checkedNames.end()) {
                    continue;
                }
                checkedNames.insert(symbolName);
                auto& symbolCodePoints = extFunc.NameCodePoints;
                if (symbolCodePoints.empty()) {
                    symbolCodePoints = calcCodePoint(symbolName);
                }
                if ( (int)symbolCodePoints.size() - (int)nameCodePoints.size() > MAX_DISTANCE ) {
                    continue;
                }
                int distance = EditDistanceCalculator.Calc(
                    std::span<const uint32_t>(nameCodePoints.data(), nameCodePoints.size()),
                    std::span<const uint32_t>(symbolCodePoints.data(), symbolCodePoints.size())
                );
                if (distance < bestSuggestion.Distance && distance <= MAX_DISTANCE) {
                    bestSuggestion.Name = symbolName;
                    bestSuggestion.Distance = distance;
                    bestSuggestion.RequiredModuleName = moduleName;
                }
            }
        }
    }

    if (bestSuggestion.Distance == INT32_MAX) {
        return std::nullopt;
    }
    return bestSuggestion;
}

std::expected<TSymbolId, TError> TNameResolver::Declare(const std::string& name, TExprPtr node, TScopePtr scope, TScopePtr funcScope) {
    auto maybeSymbolId = scope->NameToSymbolId.find(name);
    TSymbolId symbolId{-1};
    if (Options.AllowOverloads && NAst::TMaybeNode<NAst::TFunDecl>(node)) {
        auto ovIt = scope->OverloadSets.find(name);
        if (ovIt != scope->OverloadSets.end()) {
            return RegisterOverloadEntry(name, node, ovIt->second);
        }
    }

    if (maybeSymbolId != scope->NameToSymbolId.end()) {
        if (!scope->AllowsRedeclare) {
            auto& existingSymbol = Symbols[maybeSymbolId->second.Id];
            if (Options.AllowOverloads && NAst::TMaybeNode<NAst::TFunDecl>(existingSymbol.Node) && NAst::TMaybeNode<NAst::TFunDecl>(node)) {
                return StartOverloadSet(name, maybeSymbolId->second, node, scope);
            }
            std::ostringstream ss;
            ss << "Переопределение `" << existingSymbol.Name << "' уже объявлено в области видимости " << existingSymbol.ScopeId.Id;
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

TSymbolId TNameResolver::Declare(const std::string& name, NAst::TExprPtr node, TSymbolInfo parentSymbol)
{
    auto scope = Scopes[parentSymbol.DeclScopeId];
    auto funcScope = parentSymbol.FuncScopeId >= 0 ? Scopes[parentSymbol.FuncScopeId] : nullptr;
    auto res = Declare(name, node, scope, funcScope);
    if (res) {
        return res.value();
    }
    throw std::runtime_error(res.error().ToString());
}

TSymbolId TNameResolver::DeclareFunction(const std::string& name, TExprPtr node) {
    auto scope = GetOrCreateRootScope();
    auto res = Declare(name, node, scope, nullptr);
    if (!res) {
        throw std::runtime_error(std::string("failed to declare function: ") + res.error().what());
    }

    return res.value();
}

std::expected<TSymbolId, TError> TNameResolver::ResolveInstantiatedFunDecl(std::shared_ptr<TFunDecl> fdecl) {
    auto rootScope = GetOrCreateRootScope();
    auto declRes = Declare(fdecl->Name, fdecl, rootScope, nullptr);
    if (!declRes) {
        return std::unexpected(declRes.error());
    }

    auto funcScope = NewScope(rootScope, nullptr);
    fdecl->Scope = funcScope->Id.Id;
    for (auto& param : fdecl->Params) {
        auto paramRes = Declare(param->Name, param, funcScope, funcScope);
        if (!paramRes) {
            return std::unexpected(paramRes.error());
        }
    }

    auto bodyRes = Resolve(fdecl->Body, funcScope, funcScope).result();
    if (!bodyRes) {
        return std::unexpected(bodyRes.error());
    }

    GenericInstantiations.push_back(fdecl);

    return declRes.value();
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

bool TNameResolver::ParamTypesSame(const NAst::TFunDecl& a, const NAst::TFunDecl& b) {
    if (a.Params.size() != b.Params.size()) {
        return false;
    }
    for (size_t i = 0; i < a.Params.size(); ++i) {
        if (TypeKey(a.Params[i]->Type) != TypeKey(b.Params[i]->Type)) {
            return false;
        }
    }
    return true;
}

TSymbolId TNameResolver::RegisterOverloadEntry(
    const std::string& canonicalName,
    NAst::TExprPtr node,
    std::vector<TSymbolId>& overloads)
{
    auto newDecl = NAst::TMaybeNode<NAst::TFunDecl>(node).Cast();
    for (const auto& existingId : overloads) {
        auto existingDecl = NAst::TMaybeNode<NAst::TFunDecl>(GetSymbolNode(existingId)).Cast();
        if (existingDecl && ParamTypesSame(*newDecl, *existingDecl)) {
            throw std::runtime_error("overload of '" + canonicalName + "' differs only in return type");
        }
    }
    std::string synthName = "__overload_" + canonicalName + "_" + std::to_string(overloads.size());
    newDecl->Name = synthName;
    auto symId = DeclareFunction(synthName, node);
    overloads.push_back(symId);
    return symId;
}

TSymbolId TNameResolver::StartOverloadSet(
    const std::string& name,
    TSymbolId existingSymId,
    NAst::TExprPtr newNode,
    TScopePtr scope)
{
    auto& overloads = scope->OverloadSets[name];
    scope->NameToSymbolId.erase(name);
    RegisterOverloadEntry(name, Symbols[existingSymId.Id].Node, overloads);
    return RegisterOverloadEntry(name, newNode, overloads);
}

std::vector<TSymbolId> TNameResolver::LookupOverloads(const std::string& name, TScopeId scopeId) const {
    auto scope = Scopes[scopeId.Id];
    while (scope) {
        auto ovIt = scope->OverloadSets.find(name);
        if (ovIt != scope->OverloadSets.end()) {
            return ovIt->second;
        }
        auto nmIt = scope->NameToSymbolId.find(name);
        if (nmIt != scope->NameToSymbolId.end()) {
            return {nmIt->second};
        }
        scope = scope->Parent;
    }
    return {};
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

std::optional<TNameResolver::TRegisteredOp> TNameResolver::GetBinaryOp(
    const std::string& op, const NAst::TTypePtr& left, const NAst::TTypePtr& right) const
{
    auto it = ImportedBinaryOps.find({op, TypeKey(left), TypeKey(right)});
    if (it != ImportedBinaryOps.end()) return it->second;
    return std::nullopt;
}

std::optional<TNameResolver::TRegisteredOp> TNameResolver::GetUnaryOp(
    const std::string& op, const NAst::TTypePtr& operand) const
{
    auto it = ImportedUnaryOps.find({op, TypeKey(operand)});
    if (it != ImportedUnaryOps.end()) return it->second;
    return std::nullopt;
}

NAst::TTypePtr TNameResolver::LookupType(const std::string& name) const {
    auto it = ImportedTypes.find(name);
    if (it != ImportedTypes.end()) return it->second;
    return nullptr;
}

void TNameResolver::RegisterType(const std::string& name, NAst::TTypePtr underlying) {
    ImportedTypes[name] = std::move(underlying);
}

std::optional<TError> TNameResolver::RegisterTypeDecls(const NAst::TExprPtr& root) {
    auto block = TMaybeNode<TBlockExpr>(root);
    if (!block) return {};
    for (const auto& stmt : block.Cast()->Stmts) {
        auto td = TMaybeNode<TTypeDeclStmt>(stmt);
        if (!td) continue;
        auto named = TMaybeType<TNamedType>(td.Cast()->Type);
        if (!named) continue;
        auto n = named.Cast();
        if (ImportedModuleSymbols.contains(n->Name)) {
            return TError(stmt->Location, "Тип уже объявлен или импортирован: " + n->Name);
        }
        RegisterType(n->Name, n->UnderlyingType);
    }
    return {};
}

void TNameResolver::ImportUseStmts(const NAst::TExprPtr& root) {
    auto block = TMaybeNode<TBlockExpr>(root);
    if (!block) return;
    for (const auto& stmt : block.Cast()->Stmts) {
        auto use = TMaybeNode<TUseExpr>(stmt);
        if (!use) continue;
        ImportModule(use.Cast()->ModuleName);
    }
}

std::optional<std::string> TNameResolver::GetCast(const NAst::TTypePtr& from, const NAst::TTypePtr& to) const {
    auto it = ImportedCasts.find({TypeKey(from), TypeKey(to)});
    if (it == ImportedCasts.end()) return std::nullopt;
    return it->second;
}

void TNameResolver::PrintSymbols(std::ostream& os) const {
    for (const auto& symbol : Symbols) {
        os << "Symbol: " << symbol.Name << ", Scope: " << symbol.ScopeId.Id << "\n";
    }
}

void TNameResolver::RegisterModule(NRegistry::IModule* module) {
    if (!module) {
        throw std::runtime_error("Cannot register null module");
    }
    auto [it, flag] = Modules.insert({module->Name(), module});
    if (flag == false && it->second != module) {
        throw std::runtime_error("Module with conflicting name: " + module->Name());
    }
}

void TNameResolver::RegisterModuleAlias(const std::string& alias, const std::string& canonical) {
    if (!Modules.count(canonical)) {
        throw std::runtime_error(
            "Cannot register alias '" + alias + "': unknown module '" + canonical + "'");
    }
    auto [it, inserted] = ModuleAliases.insert({alias, canonical});
    if (!inserted && it->second != canonical) {
        throw std::runtime_error("Conflicting alias '" + alias + "'");
    }
}

std::string TNameResolver::ModulesList() const
{
    std::ostringstream oss;
    for (const auto& [name, module] : Modules) {
        oss << name << ",";
    }
    for (const auto& [alias, canonical] : ModuleAliases) {
        oss << alias << ",";
    }
    return oss.str().substr(0, oss.str().size() - 1); // remove last comma
}

std::vector<std::string> TNameResolver::GetAllImportedTypeNames() const {
    std::vector<std::string> names;
    names.reserve(ImportedTypes.size());
    for (const auto& [name, _] : ImportedTypes) {
        names.push_back(name);
    }
    return names;
}

std::vector<NRegistry::TLiteralSuffix> TNameResolver::GetAllImportedLiteralSuffixes() const {
    std::vector<NRegistry::TLiteralSuffix> result;
    for (const auto& modName : ImportedModules) {
        auto it = Modules.find(modName);
        if (it == Modules.end() || !it->second) continue;
        for (const auto& s : it->second->LiteralSuffixes()) {
            result.push_back(s);
        }
    }
    return result;
}

std::expected<bool, std::string> TNameResolver::ImportModule(const std::string& aliasOrName) {
    auto aliasIt = ModuleAliases.find(aliasOrName);
    const std::string& name = aliasIt != ModuleAliases.end() ? aliasIt->second : aliasOrName;
    if (ImportedModules.count(name)) {
        return false;
    }
    auto it = Modules.find(name);
    if (it == Modules.end()) {
        return std::unexpected("Неизвестный модуль: " + aliasOrName + ", доступные модули: " + ModulesList());
    }
    auto* module = it->second;

    for (const auto& fn : module->ExternalFunctions()) {
        if (fn.IsOp) continue; // operators allow overloading — no conflict check
        auto conflict = ImportedModuleSymbols.find(fn.Name);
        if (conflict != ImportedModuleSymbols.end()) {
            return std::unexpected(
                "Конфликт имён при импорте модуля " + name + ": символ " + fn.Name +
                " уже импортирован из модуля " + conflict->second);
        }
    }
    for (const auto& type : module->ExternalTypes()) {
        auto conflict = ImportedModuleSymbols.find(type.Name);
        if (conflict != ImportedModuleSymbols.end()) {
            return std::unexpected(
                "Конфликт имён при импорте модуля " + name + ": тип " + type.Name +
                " уже импортирован из модуля " + conflict->second);
        }
    }

    ImportedModules.insert(name);
    for (const auto& dep : module->Dependencies()) {
        if (!ImportedModules.count(dep)) {
            auto res = ImportModule(dep);
            if (!res) {
                ImportedModules.erase(name); // rollback
                return std::unexpected("Не удалось импортировать зависимость " + dep + " модуля " + name + ": " + res.error());
            }
        }
    }

    for (const auto& fn : module->ExternalFunctions()) {
        auto funType = std::make_shared<NAst::TFunctionType>(fn.ArgTypes, fn.ReturnType);
        std::vector<NAst::TParam> params;
        for (size_t i = 0; i < fn.ArgTypes.size(); ++i) {
            params.push_back(std::make_shared<NAst::TVarStmt>(TLocation{}, "arg" + std::to_string(i), fn.ArgTypes[i]));
        }
        auto funDecl = std::make_shared<NAst::TFunDecl>(TLocation{}, fn.Name, params, nullptr, fn.ReturnType);
        funDecl->MangledName = fn.MangledName;
        funDecl->Type = funType;
        funDecl->Ptr = fn.Ptr;
        funDecl->Packed = fn.Packed;
        funDecl->RequireArgsMaterialization = fn.RequireArgsMaterialization;
        funDecl->InlineFactory = fn.Inline;

        if (fn.IsOp) {
            ImportedOperators.push_back(funDecl);
            if (fn.Name == "cast" && fn.ArgTypes.size() == 1) {
                std::string synthName = "__cast_" + TypeKey(fn.ArgTypes[0]) + "_" + TypeKey(fn.ReturnType);
                DeclareFunction(synthName, funDecl);
                ImportedCasts[{TypeKey(fn.ArgTypes[0]), TypeKey(fn.ReturnType)}] = synthName;
            } else if (fn.ArgTypes.size() == 2) {
                std::string synthName = "__binop_" + fn.Name + "_"
                    + TypeKey(fn.ArgTypes[0]) + "_" + TypeKey(fn.ArgTypes[1]);
                DeclareFunction(synthName, funDecl);
                ImportedBinaryOps[{fn.Name, TypeKey(fn.ArgTypes[0]), TypeKey(fn.ArgTypes[1])}]
                    = {synthName, fn.ReturnType};
            } else if (fn.ArgTypes.size() == 1) {
                std::string synthName = "__unop_" + fn.Name + "_" + TypeKey(fn.ArgTypes[0]);
                DeclareFunction(synthName, funDecl);
                ImportedUnaryOps[{fn.Name, TypeKey(fn.ArgTypes[0])}]
                    = {synthName, fn.ReturnType};
            }
        } else {
            ImportedModuleSymbols[fn.Name] = name;
            DeclareFunction(fn.Name, funDecl);
        }
    }
    for (const auto& type : module->ExternalTypes()) {
        ImportedModuleSymbols[type.Name] = name;
        ImportedTypes[type.Name] = type.Type;
    }

    return true;
}


} // namespace NSemantics
} // namespace NQumir
