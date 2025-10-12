#include "transform.h"

#include <qumir/semantics/type_annotation/type_annotation.h>

#include <iostream>

namespace NQumir {
namespace NTransform {

std::expected<bool, TError> PreNameResolutionTransform(NAst::TExprPtr& expr)
{
    bool changed = TransformAst(expr, expr,
        [](const NAst::TExprPtr& node) -> NAst::TExprPtr {
            if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(node)) {
                auto ident = maybeIdent.Cast();
                if (ident->Name == "МАКСВЕЩ") {
                    // replace with constant = std::numeric_limits<double>::max()
                    return std::make_shared<NAst::TNumberExpr>(ident->Location, std::numeric_limits<double>::max());
                } else if (ident->Name == "МАКСЦЕЛ") {
                    // replace with constant = std::numeric_limits<int64_t>::max()
                    return std::make_shared<NAst::TNumberExpr>(ident->Location, std::numeric_limits<int64_t>::max());
                }
            }
            return node;
        },
        [](const NAst::TExprPtr& node) {
            return true;
        });
    return changed;
}

std::expected<bool, TError> PostTypeAnnotationTransform(NAst::TExprPtr& expr)
{
    bool changed = TransformAst(expr, expr,
        [](const NAst::TExprPtr& node) -> NAst::TExprPtr {
            if (auto maybeBinary = NAst::TMaybeNode<NAst::TBinaryExpr>(node)) {
                auto binary = maybeBinary.Cast();
                if (binary->Operator == NAst::TOperator("^")) {
                    // transform a**b into pow(a, b)
                    std::vector<NAst::TExprPtr> args;
                    args.push_back(binary->Left);
                    args.push_back(binary->Right);
                    auto leftType = binary->Left->Type;
                    auto rightType = binary->Right->Type;
                    std::string funcName = "pow";
                    if (NAst::TMaybeType<NAst::TIntegerType>(rightType)) {
                        funcName = "fpow";
                    }
                    return std::make_shared<NAst::TCallExpr>(
                        binary->Location,
                        std::make_shared<NAst::TIdentExpr>(binary->Location, funcName),
                        std::move(args));
                }
            }
            return node;
        },
        [](const NAst::TExprPtr& node) {
            return true; // transform all nodes
        });
    return changed;
}

std::expected<std::monostate, TError> Pipeline(NAst::TExprPtr& expr, NSemantics::TNameResolver& r)
{
    static constexpr int MaxIterations = 10;
    if (auto error = PreNameResolutionTransform(expr); !error) {
        return std::unexpected(error.error());
    }

    if (auto error = r.Resolve(expr)) {
        return std::unexpected(*error);
    }

    NTypeAnnotation::TTypeAnnotator annotator(r);
    std::expected<bool, TError> postResult;
    int iterations = 0;

    do {
        auto annotationResult = annotator.Annotate(expr);
        if (!annotationResult) {
            return std::unexpected(annotationResult.error());
        }
        postResult = PostTypeAnnotationTransform(expr);
        if (!postResult) {
            return std::unexpected(postResult.error());
        }
    } while (postResult.value() && ++iterations < MaxIterations);
    if (iterations == MaxIterations) {
        return std::unexpected(TError(expr->Location, "too many iterations in transform pipeline"));
    }

    return std::monostate{};
}

} // namespace NTransform
} // namespace NQumir
