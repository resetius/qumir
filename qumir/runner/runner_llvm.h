#pragma once

#include <qumir/parser/ast.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>

#include <qumir/ir/builder.h>
#include <qumir/ir/lowering/lower_ast.h>

#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_runner.h>

#include <expected>
#include <istream>
#include <optional>
#include <unordered_set>

namespace NQumir {

struct TLLVMRunnerOptions {
    bool PrintAst = false;
    bool PrintIr = false;
    bool PrintLlvm = false;
    int OptLevel = 0; // 0-3
};

class TLLVMRunner {
public:
    TLLVMRunner(TLLVMRunnerOptions options = {})
        : Options(std::move(options))
        , Builder(Module)
        , Lowerer(Module, Builder, Resolver)
        , Annotator(Resolver)
    {}

    // Parses, resolves, lowers to IR and executes via LLVM JIT.
    // Returns numeric result (if any) produced by the compiled chunk.
    std::expected<std::optional<std::string>, TError> Run(std::istream& input);

private:
    TLLVMRunnerOptions Options;
    // Persistent compiler state across Run() calls (REPL-style session)
    NIR::TModule Module;
    NIR::TBuilder Builder;
    NIR::TAstLowerer Lowerer;

    NSemantics::TNameResolver Resolver;
    NTypeAnnotation::TTypeAnnotator Annotator;

    std::unordered_set<int> PrintedChunks;
    std::unordered_set<int> PrintedLLVMChunks;
};

} // namespace NQumir
