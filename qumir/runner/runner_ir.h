#pragma once

#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>

#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/eval.h>

#include <expected>
#include <istream>
#include <optional>

#include <unordered_set>

namespace NQumir {

struct TIRRunnerOptions {
    bool PrintAst = false;
    bool PrintIr = false;
};

class TIRRunner {
public:
    TIRRunner(TIRRunnerOptions options = {})
        : Builder(Module)
        , Lowerer(Module, Builder, Resolver)
        , Annotator(Resolver)
        , Options(std::move(options))
        , Interpreter(Module, Runtime)
    {}

    // Parses, resolves, lowers to IR and interprets the code from input.
    // Returns numeric result (if any) produced by the compiled chunk.
    std::expected<std::optional<std::string>, TError> Run(std::istream& input);

private:
    NIR::TModule Module;
    NIR::TRuntime Runtime;
    NIR::TBuilder Builder;
    NIR::TAstLowerer Lowerer;
    NIR::TInterpreter Interpreter;
    TIRRunnerOptions Options;
    std::unordered_set<int> PrintedChunks;

    NSemantics::TNameResolver Resolver;
    NTypeAnnotation::TTypeAnnotator Annotator;
};

} // namespace NQumir
