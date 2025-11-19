#include "transform.h"
#include "qumir/error.h"
#include "qumir/parser/ast.h"

#include <qumir/semantics/type_annotation/type_annotation.h>

#include <iostream>
#include <limits>
#include <sstream>

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
            } else if (auto maybeCall = NAst::TMaybeNode<NAst::TCallExpr>(node)) {
                auto call = maybeCall.Cast();
                if (auto maybeCalleeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(call->Callee)) {
                    auto calleeIdent = maybeCalleeIdent.Cast();
                    if (calleeIdent->Name == "юникод" && call->Args.size() == 1) {
                        // cast symbol -> int
                        return std::make_shared<NAst::TCastExpr>(
                            call->Location,
                            call->Args[0],
                            std::make_shared<NAst::TIntegerType>());
                    } else if (calleeIdent->Name == "юнисимвол" && call->Args.size() == 1) {
                        // cast int -> symbol
                        return std::make_shared<NAst::TCastExpr>(
                            call->Location,
                            call->Args[0],
                            std::make_shared<NAst::TSymbolType>());
                    }
                }
            } else if (auto maybeAssert = NAst::TMaybeNode<NAst::TAssertStmt>(node)) {
                // rewrite assert statement into a call: __ensure(condition, "condition_text")
                auto assertStmt = maybeAssert.Cast();
                std::ostringstream oss;
                if (assertStmt->Expr) {
                    oss << *assertStmt->Expr;
                } else {
                    oss << "<empty>";
                }
                std::vector<NAst::TExprPtr> args;
                args.push_back(assertStmt->Expr);
                args.push_back(std::make_shared<NAst::TStringLiteralExpr>(assertStmt->Location, oss.str()));
                return std::make_shared<NAst::TCallExpr>(
                    assertStmt->Location,
                    std::make_shared<NAst::TIdentExpr>(assertStmt->Location, "__ensure"),
                    std::move(args));
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
                if (binary->Operator == NAst::TOperator("+")) {
                    bool leftStr = NAst::TMaybeType<NAst::TStringType>(binary->Left->Type);
                    bool rightStr = NAst::TMaybeType<NAst::TStringType>(binary->Right->Type);

                    // string + string (string + symbol handled with implicit cast)
                    if (leftStr && rightStr) {
                        auto call = std::make_shared<NAst::TCallExpr>(binary->Location,
                            std::make_shared<NAst::TIdentExpr>(binary->Location, "str_concat"),
                            std::vector<NAst::TExprPtr>{binary->Left, binary->Right});
                        return call;
                    }
                }
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
                    if (auto refType = NAst::TMaybeType<NAst::TReferenceType>(type)) {
                        type = refType.Cast()->ReferencedType;
                    }
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
                    } else if (NAst::TMaybeType<NAst::TBoolType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_bool"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TStringType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_string"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TSymbolType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_symbol"),
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

                    if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(arg)) {
                        auto ident = maybeIdent.Cast();
                        // assign to ident
                        call = std::make_shared<NAst::TAssignExpr>(
                            input->Location,
                            ident->Name,
                            call);
                    } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(arg)) {
                        auto index = maybeIndex.Cast();
                        auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection);
                        if (!maybeIdent) {
                            errors.push_back(TError(arg->Location, "input argument index expression must index an identifier"));
                            continue;
                        }
                        auto ident = maybeIdent.Cast();
                        // assign to index
                        call = std::make_shared<NAst::TArrayAssignExpr>(
                            input->Location,
                            ident->Name,
                            std::vector<NAst::TExprPtr>{index->Index},
                            call);
                    } else if (auto maybeMultiIndex = NAst::TMaybeNode<NAst::TMultiIndexExpr>(arg)) {
                        auto multiIndex = maybeMultiIndex.Cast();
                        auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(multiIndex->Collection);
                        if (!maybeIdent) {
                            errors.push_back(TError(arg->Location, "input argument multi-index expression must index an identifier"));
                            continue;
                        }
                        auto ident = maybeIdent.Cast();
                        // assign to multi-index
                        call = std::make_shared<NAst::TArrayAssignExpr>(
                            input->Location,
                            ident->Name,
                            multiIndex->Indices,
                            call);
                    } else {
                        errors.push_back(TError(arg->Location, "input argument must be an identifier or an index expression"));
                    }

                    stmts.push_back(call);
                }
                return std::make_shared<NAst::TBlockExpr>(input->Location, std::move(stmts));
            } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(node)) {
                auto index = maybeIndex.Cast();
                if (NAst::TMaybeType<NAst::TStringType>(index->Collection->Type)) {
                    // rewrite to str_symbol_at(collection, index)
                    auto funcNameIdent = std::make_shared<NAst::TIdentExpr>(index->Location, "str_symbol_at");
                    auto symbolAt = std::make_shared<NAst::TCallExpr>(index->Location, funcNameIdent, std::vector<NAst::TExprPtr>{
                        index->Collection,
                        index->Index,
                    });
                    symbolAt->Type = index->Type;
                    return symbolAt;
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
            } else if (auto maybeCast = NAst::TMaybeNode<NAst::TCastExpr>(node)) {
                auto cast = maybeCast.Cast();
                if (NAst::TMaybeType<NAst::TStringType>(cast->Type) && NAst::TMaybeType<NAst::TSymbolType>(cast->Operand->Type)) {
                    // symbol -> string cast
                    auto funcNameIdent = std::make_shared<NAst::TIdentExpr>(cast->Location, "str_from_unicode");
                    auto castCall = std::make_shared<NAst::TCallExpr>(cast->Location, funcNameIdent, std::vector<NAst::TExprPtr>{
                        cast->Operand
                    });
                    castCall->Type = cast->Type;
                    return castCall;
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

    auto generateBounds = [&](std::shared_ptr<NAst::TVarStmt> var, const NSemantics::TSymbolInfo& symbolInfo) -> auto {
        auto block = std::make_shared<NAst::TBlockExpr>(var->Location, std::vector<NAst::TExprPtr>{});
        auto boundaries = std::move(var->Bounds);
        block->Scope = scopeId;
        block->SkipDestructors = true;
        int i = boundaries.size() - 1;

        auto declare = [&](const std::string& name) {
            auto newVar = std::make_shared<NAst::TVarStmt>(
                var->Location,
                name,
                std::make_shared<NAst::TIntegerType>());
            block->Stmts.push_back(newVar);
            context.Declare(name, newVar, symbolInfo);
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
            std::string lboundName = "$$" + var->Name + "_lbound" + std::to_string(i);
            std::string dimSizeName = "$$" + var->Name + "_dimsize" + std::to_string(i);
            std::string mulAccName = "$$" + var->Name + "_mulacc" + std::to_string(i);
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
        return block;
    };

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
                    auto symbolId = context.Lookup(var->Name, NSemantics::TScopeId{scopeId});
                    if (!symbolId) {
                        errors.push_back(TError(var->Location, "undefined identifier: " + var->Name + " in scope " + std::to_string(scopeId)));
                        return node;
                    }

                    auto block = generateBounds(var, *symbolId);
                    block->Stmts.push_back(var);
                    return block;
                }
            } else if (auto maybeFunDecl = NAst::TMaybeNode<NAst::TFunDecl>(node)) {
                auto funDecl = maybeFunDecl.Cast();
                if (!funDecl->Body) {
                    // external function declaration, skip
                    return node;
                }

                std::vector<std::shared_ptr<NAst::TBlockExpr>> preBlocks;
                for (auto& param : funDecl->Params) {
                    if (param->Bounds.empty()) {
                        continue;
                    }

                    auto symbolId = context.Lookup(param->Name, NSemantics::TScopeId{funDecl->Scope});;
                    if (!symbolId) {
                        errors.push_back(TError(param->Location, "undefined identifier: " + param->Name + " in scope " + std::to_string(funDecl->Scope)));
                        return node;
                    }

                    auto block = generateBounds(param, *symbolId);
                    preBlocks.emplace_back(std::move(block));
                }
                auto functionBody = std::move(funDecl->Body);
                auto newBody = std::make_shared<NAst::TBlockExpr>(functionBody->Location, std::vector<NAst::TExprPtr>{});
                newBody->Scope = functionBody->Scope;
                for (auto& preBlock : preBlocks) {
                    for (auto& stmt : preBlock->Stmts) {
                        newBody->Stmts.push_back(stmt);
                    }
                }
                for (auto& stmt : functionBody->Stmts) {
                    newBody->Stmts.push_back(stmt);
                }
                funDecl->Body = newBody;
                return funDecl;
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

    // 1. extract using directives
    if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(expr)) {
        // 2. using must be the first statements in the block
        // 3. we don't support > 1 using directives
        auto block = maybeBlock.Cast();
        auto& stmts = block->Stmts;
        if (!stmts.empty()) {
            auto first = stmts.front();
            if (auto maybeUse = NAst::TMaybeNode<NAst::TUseExpr>(first)) {
                auto use = maybeUse.Cast();
                if (!r.ImportModule(use->ModuleName)) {
                    return std::unexpected(TError(use->Location, "unknown module: " + use->ModuleName));
                }
                stmts.erase(stmts.begin());
            }
        }
    }

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
