#include "transform.h"
#include "qumir/error.h"
#include "qumir/parser/ast.h"

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
    std::list<TError> errors;
    bool changed = TransformAst(expr, expr,
        [&](const NAst::TExprPtr& node) -> NAst::TExprPtr {
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
            } else if (auto maybeOutput = NAst::TMaybeNode<NAst::TOutputExpr>(node)) {
                auto output = maybeOutput.Cast();
                // transform output(a, b, c) into a series of calls to output_xxx functions
                std::vector<NAst::TExprPtr> stmts;
                for (const auto& arg : output->Args) {
                    NAst::TExprPtr call;
                    auto type = arg->Type;
                    if (NAst::TMaybeType<NAst::TFloatType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_double"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TIntegerType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_int64"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TStringType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_string"),
                            std::move(args));
                    } else {
                        errors.push_back(TError(arg->Location, "output argument must be int, float, or string, got: " + (type ? type->ToString() : "unknown")));
                    }
                    stmts.push_back(call);
                }
                return std::make_shared<NAst::TBlockExpr>(output->Location, stmts);
            } else if (auto maybeInput = NAst::TMaybeNode<NAst::TInputExpr>(node)) {
                auto input = maybeInput.Cast();
                // transform input(type) into a call to input_xxx function
                std::vector<NAst::TExprPtr> stmts;
                for (const auto& arg : input->Args) {
                    NAst::TExprPtr call;
                    auto type = arg->Type;
                    if (NAst::TMaybeType<NAst::TFloatType>(type)) {
                        call = std::make_shared<NAst::TCallExpr>(
                            input->Location,
                            std::make_shared<NAst::TIdentExpr>(input->Location, "input_double"),
                            std::vector<NAst::TExprPtr>{});
                    } else if (NAst::TMaybeType<NAst::TIntegerType>(type)) {
                        call = std::make_shared<NAst::TCallExpr>(
                            input->Location,
                            std::make_shared<NAst::TIdentExpr>(input->Location, "input_int64"),
                            std::vector<NAst::TExprPtr>{});
                    } else {
                        errors.push_back(TError(arg->Location, "input argument must be float or int64, got: " + type->ToString()));
                    }
                    stmts.push_back(call);
                }
                return std::make_shared<NAst::TBlockExpr>(input->Location, std::move(stmts));
            } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(node)) {
                auto index = maybeIndex.Cast();
                if (NAst::TMaybeType<NAst::TStringType>(index->Collection->Type)) {
                    // rewrite to str_slice(collection, index, index)
                    auto funcNameIdent = std::make_shared<NAst::TIdentExpr>(index->Location, "str_slice");
                    auto slice = std::make_shared<NAst::TCallExpr>(index->Location, funcNameIdent, std::vector<NAst::TExprPtr>{
                        index->Collection,
                        index->Index,
                        index->Index
                    });
                    slice->Type = index->Type;
                    return slice;
                }
            } else if (auto maybeSlice = NAst::TMaybeNode<NAst::TSliceExpr>(node)) {
                auto slice = maybeSlice.Cast();
                if (NAst::TMaybeType<NAst::TStringType>(slice->Collection->Type)) {
                    // rewrite to str_slice(collection, start, end)
                    auto funcNameIdent = std::make_shared<NAst::TIdentExpr>(slice->Location, "str_slice");
                    auto sliceCall = std::make_shared<NAst::TCallExpr>(slice->Location, funcNameIdent, std::vector<NAst::TExprPtr>{
                        slice->Collection,
                        slice->Start,
                        slice->End
                    });
                    sliceCall->Type = slice->Type;
                    return sliceCall;
                }
            }
            return node;
        },
        [](const NAst::TExprPtr& node) {
            return true; // transform all nodes
        });

    if (changed) {
        int lastScope = -1;
        // update scopes in block nodes
        PreorderTransformAst(expr, expr,
            [&](const NAst::TExprPtr& node) -> NAst::TExprPtr {
                if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(node)) {
                    auto block = maybeBlock.Cast();
                    if (block->Scope == -1) {
                        block->Scope = lastScope;
                    } else {
                        lastScope = block->Scope;
                    }
                }
                return node;
            },
            [](const NAst::TExprPtr& node) {
                return !NAst::TMaybeNode<NAst::TBinaryExpr>(node);
            });
    }

    if (!errors.empty()) {
        return std::unexpected(TError(expr->Location, errors));
    }
    return changed;
}

// replace 'ident' with 'ident()' if ident is a function with no parameters
std::expected<bool, TError> PostNameResolutionTransform(NAst::TExprPtr& expr, NSemantics::TNameResolver& context) {
    std::list<TError> errors;
    int scopeId = -1;
    // Need PreorderTransformAst for scope tracking: update scopeId when entering a block
    // TODO: implement scrope tracking in TransformAst and use it here
    bool changed = PreorderTransformAst(expr, expr,
        [&](const NAst::TExprPtr& node) -> NAst::TExprPtr {
            if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(node)) {
                if (scopeId == -1) {
                    return node;
                }

                auto ident = maybeIdent.Cast();
                auto symbolId = context.Lookup(ident->Name, NSemantics::TScopeId{scopeId});
                if (!symbolId) {
                    errors.push_back(TError(ident->Location, "undefined identifier: " + ident->Name + " in scope " + std::to_string(scopeId)));
                    return node;
                }
                auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
                if (!sym) {
                    errors.push_back(TError(ident->Location, "invalid identifier symbol: " + ident->Name));
                    return node;
                }
                if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(sym)) {
                    auto fun = maybeFun.Cast();
                    if (fun->Params.empty()) {
                        // function call without brackets
                        auto call = std::make_shared<NAst::TCallExpr>(ident->Location, ident, std::vector<NAst::TExprPtr>{});
                        call->Type = fun->RetType;
                        return call;
                    }
                }
                return node;
            } else if (auto maybeVar = NAst::TMaybeNode<NAst::TVarStmt>(node)) {
                auto var = maybeVar.Cast();
                if (!var->Bounds.empty() && NAst::TMaybeType<NAst::TArrayType>(node->Type)) {
                    auto block = std::make_shared<NAst::TBlockExpr>(var->Location, std::vector<NAst::TExprPtr>{});
                    auto boundaries = std::move(var->Bounds);
                    block->Scope = scopeId;
                    block->SkipDestructors = true;
                    int i = boundaries.size() - 1;

                    auto symbolId = context.Lookup(var->Name, NSemantics::TScopeId{scopeId});
                    if (!symbolId) {
                        errors.push_back(TError(var->Location, "undefined identifier: " + var->Name + " in scope " + std::to_string(scopeId)));
                        return node;
                    }

                    auto declare = [&](const std::string& name) {
                        auto newVar = std::make_shared<NAst::TVarStmt>(
                            var->Location,
                            name,
                            std::make_shared<NAst::TIntegerType>());
                        block->Stmts.push_back(newVar);
                        context.Declare(name, newVar, *symbolId);
                    };

                    auto assign = [&](const std::string& name, NAst::TExprPtr value) {
                        block->Stmts.push_back(std::make_shared<NAst::TAssignExpr>(
                            var->Location,
                            name,
                            value));
                    };

                    auto ident = [&](const std::string& name) {
                        return std::make_shared<NAst::TIdentExpr>(var->Location, name);
                    };

                    auto one = std::make_shared<NAst::TNumberExpr>(var->Location, (int64_t) 1);
                    NAst::TExprPtr prevDivSize = one;

                    for (; i >= 0; --i) {
                        auto [lbound, rbound] = boundaries[i];
                        std::string lboundName = "__" + var->Name + "_lbound" + std::to_string(i);
                        std::string dimSizeName = "__" + var->Name + "_dimsize" + std::to_string(i);
                        std::string mulAccName = "__" + var->Name + "_mulacc" + std::to_string(i);
                        // lboundName = lbound
                        // dimSizeName = rbound - lbound + 1
                        // mulAccName = prevDivSize * dimSizeName
                        // prevDivSize = mulAccName
                        // 1. declare variables (TVarStmts)
                        declare(lboundName);
                        declare(dimSizeName);
                        declare(mulAccName);
                        // 2. assign variables (TAssignExpr)
                        assign(lboundName, lbound);
                        assign(dimSizeName, std::make_shared<NAst::TBinaryExpr>(
                                var->Location,
                                NAst::TOperator("+"),
                                    std::make_shared<NAst::TBinaryExpr>(
                                    var->Location,
                                    NAst::TOperator("-"),
                                    rbound,
                                    lbound),
                                one));
                        auto mulAccExpr = std::make_shared<NAst::TBinaryExpr>(
                            var->Location, NAst::TOperator("*"), prevDivSize, ident(dimSizeName));
                        assign(mulAccName, mulAccExpr);
                        prevDivSize = mulAccExpr;
                    }
                    block->Stmts.push_back(var);
                    return block;
                }
            } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(node)) {
                auto block = maybeBlock.Cast();
                scopeId = block->Scope;
            }
            return node;
        },
        [](const NAst::TExprPtr& node) {
            return true; // transform all nodes
        });
    if (!errors.empty()) {
        return std::unexpected(TError(expr->Location, errors));
    }
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

    if (auto error = PostNameResolutionTransform(expr, r); !error) {
        return std::unexpected(error.error());
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
