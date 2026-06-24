#pragma once

#include <qumir/location.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/pragma.h>

#include <filesystem>
#include <string>
#include <vector>

namespace NQumir {
namespace NFrontend {

enum class EExportKind {
    Function,
    Type,
    Global,
};

// Location is retained so later conflict diagnostics can point at the
// declaring position.
struct TSourceModuleExport {
    std::string Name;
    EExportKind Kind;
    TLocation Location;
};

// Cache state of a module while the loader resolves the dependency graph.
// Re-encountering Loading marks a dependency cycle.
enum class ESourceModuleState {
    Loading,
    Loaded,
    Failed,
};

// A parsed and validated `.oz` module: its interface (exports), its `use`
// dependencies and the fresh AST built for this compilation session.
struct TSourceModule {
    std::string Name;
    std::filesystem::path Path;
    NAst::TExprPtr Ast;

    // All `use` names in source order. SourceDependencies is the subset that
    // resolved to `.oz` files; the rest are runtime modules left for a later
    // import stage.
    std::vector<std::string> Dependencies;
    std::vector<std::string> SourceDependencies;

    std::vector<TSourceModuleExport> Exports;
    std::vector<NAst::TPragma> Pragmas;

    std::vector<std::string> ExportedOfKind(EExportKind kind) const {
        std::vector<std::string> result;
        for (const auto& e : Exports) {
            if (e.Kind == kind) {
                result.push_back(e.Name);
            }
        }
        return result;
    }

    std::vector<std::string> ExportedFunctions() const { return ExportedOfKind(EExportKind::Function); }
    std::vector<std::string> ExportedTypes() const { return ExportedOfKind(EExportKind::Type); }
    std::vector<std::string> ExportedGlobals() const { return ExportedOfKind(EExportKind::Global); }
};

} // namespace NFrontend
} // namespace NQumir
