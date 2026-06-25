#include "source_module_loader.h"

#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/parser/type.h>

#include <algorithm>
#include <fstream>
#include <system_error>

namespace NQumir {
namespace NFrontend {

namespace fs = std::filesystem;
using namespace NAst;

namespace {

TError Diag(const fs::path& path, const TLocation& loc, const std::string& msg) {
    return TError(loc, path.string() + ":" + std::to_string(loc.Line) + ":"
        + std::to_string(loc.Column) + ": " + msg);
}

std::string ModuleName(const fs::path& path) {
    return path.stem().string();
}

std::expected<TExprPtr, TError> ParseFile(const fs::path& path, std::vector<TPragma>& pragmas) {
    std::ifstream in(path);
    if (!in) {
        return std::unexpected(TError(path.string() + ": не удалось открыть файл"));
    }
    NCore::TTokenStream stream(in);
    NCore::TParser parser;
    auto parsed = parser.Parse(stream);
    if (!parsed) {
        return std::unexpected(TError(path.string() + ": " + parsed.error().ToString()));
    }
    pragmas = std::move(parser.Pragmas);
    return parsed;
}

std::optional<TError> CollectInterface(const fs::path& path, TSourceModule& module) {
    auto block = TMaybeNode<TBlockExpr>(module.Ast);
    if (!block) {
        return Diag(path, module.Ast ? module.Ast->Location : TLocation{},
            "корневое выражение модуля должно быть блоком");
    }
    for (const auto& stmt : block.Cast()->Stmts) {
        if (auto use = TMaybeNode<TUseExpr>(stmt)) {
            module.Dependencies.push_back(use.Cast()->ModuleName);
        } else if (auto type = TMaybeNode<TTypeDeclStmt>(stmt)) {
            auto named = TMaybeType<TNamedType>(type.Cast()->Type);
            module.Exports.push_back({named.Cast()->Name, EExportKind::Type, stmt->Location});
        } else if (auto fun = TMaybeNode<TFunDecl>(stmt)) {
            const auto& name = fun.Cast()->Name;
            if (name == "<main>") {
                return Diag(path, stmt->Location, "импортируемый модуль не может объявлять `<main>`");
            }
            module.Exports.push_back({name, EExportKind::Function, stmt->Location});
        } else if (auto var = TMaybeNode<TVarStmt>(stmt)) {
            module.Exports.push_back({var.Cast()->Name, EExportKind::Global, stmt->Location});
        } else {
            return Diag(path, stmt->Location,
                "недопустимое верхнеуровневое выражение в модуле: `" + std::string(stmt->NodeName()) + "'");
        }
    }
    return std::nullopt;
}

} // namespace

void TSourceModuleLoader::AddSearchPath(fs::path dir) {
    SearchPaths.push_back(std::move(dir));
}

std::expected<std::monostate, TError> TSourceModuleLoader::RegisterSourceModule(fs::path file) {
    std::error_code ec;
    auto canonical = fs::canonical(file, ec);
    if (ec) {
        return std::unexpected(TError(file.string() + ": не удалось зарегистрировать модуль: " + ec.message()));
    }
    auto name = ModuleName(canonical);
    auto [it, inserted] = ExplicitModules.try_emplace(name, canonical);
    if (!inserted && it->second != canonical) {
        return std::unexpected(TError("модуль `" + name + "' уже зарегистрирован из другого файла: "
            + it->second.string() + " и " + canonical.string()));
    }
    return std::monostate{};
}

std::optional<fs::path> TSourceModuleLoader::ResolvePath(const std::string& name) const {
    if (auto it = ExplicitModules.find(name); it != ExplicitModules.end()) {
        return it->second;
    }
    for (const auto& dir : SearchPaths) {
        auto candidate = dir / (name + ".oz");
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) {
            auto canonical = fs::canonical(candidate, ec);
            if (!ec) {
                return canonical;
            }
        }
    }
    return std::nullopt;
}

std::expected<const TSourceModule*, TError> TSourceModuleLoader::Load(const std::string& name) {
    auto path = ResolvePath(name);
    if (!path) {
        std::string msg = "модуль `" + name + "' не найден";
        if (!SearchPaths.empty()) {
            msg += ", искал в:";
            for (const auto& dir : SearchPaths) {
                msg += "\n  - " + dir.string();
            }
        }
        return std::unexpected(TError(msg));
    }
    return LoadResolved(name, *path);
}

std::expected<const TSourceModule*, TError> TSourceModuleLoader::LoadResolved(
    const std::string& name,
    const fs::path& path)
{
    if (auto it = Cache.find(name); it != Cache.end()) {
        switch (it->second.State) {
        case ESourceModuleState::Loaded:
            return it->second.Module.get();
        case ESourceModuleState::Loading: {
            std::string chain;
            auto start = std::find(LoadStack.begin(), LoadStack.end(), name);
            for (auto i = start; i != LoadStack.end(); ++i) {
                chain += *i + " -> ";
            }
            chain += name;
            return std::unexpected(TError("обнаружен цикл зависимостей модулей: " + chain));
        }
        case ESourceModuleState::Failed:
            return std::unexpected(TError("модуль `" + name + "' ранее не загрузился"));
        }
    }

    auto& entry = Cache[name];
    entry.State = ESourceModuleState::Loading;
    LoadStack.push_back(name);

    auto fail = [&](TError error) -> std::unexpected<TError> {
        entry.State = ESourceModuleState::Failed;
        LoadStack.pop_back();
        return std::unexpected(std::move(error));
    };

    auto module = std::make_unique<TSourceModule>();
    module->Name = name;
    module->Path = path;

    auto parsed = ParseFile(path, module->Pragmas);
    if (!parsed) {
        return fail(parsed.error());
    }
    module->Ast = std::move(*parsed);

    if (auto error = CollectInterface(path, *module)) {
        return fail(std::move(*error));
    }

    entry.Module = std::move(module);

    for (const auto& dep : entry.Module->Dependencies) {
        auto depPath = ResolvePath(dep);
        if (!depPath) {
            continue; // external (runtime) module, resolved by a later stage
        }
        entry.Module->SourceDependencies.push_back(dep);
        if (auto loaded = LoadResolved(dep, *depPath); !loaded) {
            return fail(loaded.error());
        }
    }

    LoadStack.pop_back();
    entry.State = ESourceModuleState::Loaded;
    CompletionOrder.push_back(name);
    return entry.Module.get();
}

std::vector<const TSourceModule*> TSourceModuleLoader::TopologicalOrder() const {
    std::vector<const TSourceModule*> result;
    result.reserve(CompletionOrder.size());
    for (const auto& name : CompletionOrder) {
        result.push_back(Cache.at(name).Module.get());
    }
    return result;
}

const TSourceModule* TSourceModuleLoader::Find(const std::string& name) const {
    auto it = Cache.find(name);
    if (it == Cache.end() || it->second.State != ESourceModuleState::Loaded) {
        return nullptr;
    }
    return it->second.Module.get();
}

} // namespace NFrontend
} // namespace NQumir
