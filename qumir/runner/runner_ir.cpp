#include "runner_ir.h"

#include <qumir/parser/lexer.h>

#include <iostream>
#include <sstream>

namespace NQumir {

using namespace NIR;
using namespace NAst;

std::expected<std::optional<std::string>, TError> TIRRunner::Run(std::istream& input) {
    TTokenStream ts(input);
    TParser p;
    auto parsed = p.parse(ts);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    auto ast = std::move(parsed.value());
    auto scope = Resolver.GetOrCreateRootScope();
    scope->AllowsRedeclare = true; // TODO: move to options?
    scope->RootLevel = false;
    auto error = Resolver.Resolve(ast);
    if (error) {
        return std::unexpected(*error);
    }

    auto annotationResult = Annotator.Annotate(ast);
    if (!annotationResult) {
        return std::unexpected(annotationResult.error());
    }
    ast = annotationResult.value();
    if (Options.PrintAst) {
        std::cerr << "=========== AST: ===========\n";
        std::cerr << *ast << std::endl;
        std::cerr << "============================\n\n";
    }

    // Lower to IR: we build a fresh __repl function chunk per call
    auto lowerRes = Lowerer.LowerTopRepl(ast);
    if (!lowerRes) {
        return std::unexpected(lowerRes.error());
    }
    auto* fun = lowerRes.value();

    if (Options.PrintIr) {
        std::cerr << "=========== IR: ============\n";
        for (const auto& function : Module.Functions) {
            if (PrintedChunks.insert(function.UniqueId).second) {
                std::ostringstream oss;
                function.Print(oss, Module);
                std::cerr << oss.str() << std::endl;
            }
        }
        std::cerr << "============================\n\n";
    }

    // Interpret
    auto res = Interpreter.Eval(*fun, {});

    // Return the raw numeric result, or std::nullopt if no value
    return res;
}

} // namespace NQumir
