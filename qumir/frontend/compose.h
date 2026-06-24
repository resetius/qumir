#pragma once

#include <qumir/error.h>
#include <qumir/frontend/source_module.h>
#include <qumir/parser/ast.h>
#include <qumir/parser/pragma.h>

#include <expected>
#include <string>
#include <vector>

namespace NQumir {
namespace NFrontend {

struct TComposeResult {
    NAst::TExprPtr Ast;
    std::vector<NAst::TPragma> Pragmas;
};

// Builds a single compilation unit from the imported source modules and the
// main program. `modules` is in dependency-first order; the main program is
// appended last. The result root block orders declarations as the IR lowering
// requires: external (runtime) `use`s, then types, then globals, then
// functions. `use`s that name a composed source module are dropped — those
// modules are inlined directly. Pragmas of all units are merged (conflicting
// values for the same group is an error), and duplicate exported names are
// reported with their originating files before name resolution runs.
std::expected<TComposeResult, TError> Compose(
    const std::vector<const TSourceModule*>& modules,
    const NAst::TExprPtr& mainAst,
    const std::vector<NAst::TPragma>& mainPragmas,
    const std::string& mainLabel);

// Frontend entry shared by runners: loads the `.oz` source modules referenced
// by `use` in the core program and composes them with the main AST. When no
// `use` resolves to a source module, returns the main AST unchanged with
// `corePragmas`.
std::expected<TComposeResult, TError> LoadAndCompose(
    const NAst::TExprPtr& mainAst,
    const std::vector<NAst::TPragma>& corePragmas,
    const std::vector<std::string>& searchPaths);

} // namespace NFrontend
} // namespace NQumir
