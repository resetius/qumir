#include "transform.h"
#include "qumir/error.h"
#include "qumir/parser/ast.h"
#include "qumir/parser/core/printer.h"

#include <qumir/semantics/type_annotation/type_annotation.h>
#include <qumir/semantics/definite_assignment/definite_assignment.h>

#include <algorithm>
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
                    oss << assertStmt->Expr;
                } else {
                    oss << "<empty>";
                }
                oss << " @ " << assertStmt->Location.ToString();
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

std::expected<bool, TError> PostTypeAnnotationTransform(NAst::TExprPtr& expr, NSemantics::TNameResolver& context)
{
    std::list<TError> errors;
    bool changed = TransformAst(expr, expr,
        [&](const NAst::TExprPtr& node) -> NAst::TExprPtr {
            // Inline substitution: replace TCallExpr to functions with InlineFactory with their AST.
            // Args must be annotated (Type set) so the factory receives typed expressions.
            // The loop then re-runs type annotation on the replacement.
            if (auto maybeCall = NAst::TMaybeNode<NAst::TCallExpr>(node)) {
                auto call = maybeCall.Cast();
                if (call->Type) { // only inline already-annotated calls
                    if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(call->Callee)) {
                        auto symId = context.Lookup(maybeIdent.Cast()->Name, NSemantics::TScopeId{0});
                        if (symId) {
                            auto symNode = context.GetSymbolNode(NSemantics::TSymbolId{symId->Id});
                            if (auto maybeFunDecl = NAst::TMaybeNode<NAst::TFunDecl>(symNode)) {
                                auto funDecl = maybeFunDecl.Cast();
                                if (funDecl->InlineFactory) {
                                    bool argsReady = std::all_of(call->Args.begin(), call->Args.end(),
                                        [](const NAst::TExprPtr& a){ return a && a->Type; });
                                    if (argsReady) {
                                        return (*funDecl->InlineFactory)(call->Args);
                                    }
                                }
                            }
                        }
                    }
                }
                return node;
            }

            // Transform string element assignment: s[i] = 'c' => s = str_replace_sym(s, 'c', i)
            if (auto maybeArrayAssign = NAst::TMaybeNode<NAst::TArrayAssignExpr>(node)) {
                auto arrayAssign = maybeArrayAssign.Cast();
                auto unrefType = UnwrapReferenceType(arrayAssign->Value->Type);
                auto maybeSymbolType = NAst::TMaybeType<NAst::TSymbolType>(unrefType);
                if (!maybeSymbolType) {
                    return node;
                }
                if (arrayAssign->Indices.size() != 1) {
                    errors.push_back(TError(arrayAssign->Location, "string element assignment must have exactly one index"));
                    return node;
                }
                auto call = std::make_shared<NAst::TCallExpr>(arrayAssign->Location,
                    std::make_shared<NAst::TIdentExpr>(arrayAssign->Location, "str_replace_sym"),
                    std::vector<NAst::TExprPtr>{
                        std::make_shared<NAst::TIdentExpr>(arrayAssign->Location, arrayAssign->Name),
                        arrayAssign->Value,
                        arrayAssign->Indices[0]});

                auto assign = std::make_shared<NAst::TAssignExpr>(arrayAssign->Location,
                    arrayAssign->Name,
                    call);
                return assign;
            }
            if (auto maybeBinary = NAst::TMaybeNode<NAst::TBinaryExpr>(node)) {
                auto binary = maybeBinary.Cast();

                auto leftType = UnwrapReferenceType(binary->Left->Type);
                auto rightType = UnwrapReferenceType(binary->Right->Type);
                bool leftStr = NAst::TMaybeType<NAst::TStringType>(leftType);
                bool rightStr = NAst::TMaybeType<NAst::TStringType>(rightType);

                if (leftStr || rightStr) {
                    // string + string (string + symbol handled with implicit cast)
                    if (binary->Operator == NAst::TOperator("+")) {
                        auto call = std::make_shared<NAst::TCallExpr>(binary->Location,
                            std::make_shared<NAst::TIdentExpr>(binary->Location, "str_concat"),
                            std::vector<NAst::TExprPtr>{binary->Left, binary->Right});
                        return call;
                    }
                    if ((binary->Operator == "==" || binary->Operator == "!=" || binary->Operator == "<=" || binary->Operator == ">=" || binary->Operator == "<" || binary->Operator == ">")) {
                        auto call = std::make_shared<NAst::TCallExpr>(binary->Location,
                            std::make_shared<NAst::TIdentExpr>(binary->Location, "str_compare"),
                            std::vector<NAst::TExprPtr>{binary->Left, binary->Right});
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(call);
                        args.push_back(std::make_shared<NAst::TNumberExpr>(binary->Location, (int64_t)0));
                        return std::make_shared<NAst::TBinaryExpr>(binary->Location, binary->Operator, args[0], args[1]);
                    }
                }

                if (binary->Operator == NAst::TOperator("^")) {
                    // transform a**b into pow(a, b)
                    std::vector<NAst::TExprPtr> args;
                    args.push_back(binary->Left);
                    args.push_back(binary->Right);
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
                if (output->Args.size() == 0) {
                    errors.push_back(TError(output->Location, "output requires at least one argument"));
                    return node;
                }
                int i = 0;
                const auto& arg0 = output->Args[0];
                bool hasFileArg = false;
                if (NAst::TMaybeType<NAst::TFileType>(arg0.Expr->Type)) {
                    // first argument is a file, set output file
                    auto call = std::make_shared<NAst::TCallExpr>(
                        output->Location,
                        std::make_shared<NAst::TIdentExpr>(output->Location, "output_set_file"),
                        std::vector<NAst::TExprPtr>{ arg0.Expr });
                    stmts.push_back(call);
                    hasFileArg = true;
                    i = 1;
                }

                for (; i < (int)output->Args.size(); ++i) {
                    const auto& arg = output->Args[i];
                    NAst::TExprPtr call;
                    auto type = arg.Expr->Type;
                    if (auto refType = NAst::TMaybeType<NAst::TReferenceType>(type)) {
                        type = refType.Cast()->ReferencedType;
                    }
                    auto width = arg.Width;
                    auto prec = arg.Precision;
                    if (width) {
                        if (!NAst::TMaybeType<NAst::TIntegerType>(width->Type)) {
                            errors.push_back(TError(width->Location, "width argument must be of integer type"));
                            return node;
                        }
                    }
                    if (prec) {
                        if (!NAst::TMaybeType<NAst::TIntegerType>(prec->Type)) {
                            errors.push_back(TError(prec->Location, "precision argument must be of integer type"));
                            return node;
                        }
                    }
                    if (NAst::TMaybeType<NAst::TFloatType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg.Expr);
                        if (width) {
                            args.push_back(width);
                        } else {
                            args.push_back(std::make_shared<NAst::TNumberExpr>(output->Location, (int64_t)0));
                        }
                        if (prec) {
                            args.push_back(prec);
                        } else {
                            args.push_back(std::make_shared<NAst::TNumberExpr>(output->Location, (int64_t)-1));
                        }
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_double"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TIntegerType>(type)) {
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg.Expr);
                        if (width) {
                            args.push_back(width);
                        } else {
                            args.push_back(std::make_shared<NAst::TNumberExpr>(output->Location, (int64_t)0));
                        }
                        if (prec) {
                            errors.push_back(TError(prec->Location, "precision argument is not applicable for integer output"));
                            return node;
                        }
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_int64"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TBoolType>(type)) {
                        if (width || prec) {
                            errors.push_back(TError(output->Location, "width and precision arguments are not applicable for boolean output"));
                            return node;
                        }
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg.Expr);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_bool"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TStringType>(type)) {
                        if (width || prec) {
                            errors.push_back(TError(output->Location, "width and precision arguments are not applicable for string output"));
                            return node;
                        }
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg.Expr);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_string"),
                            std::move(args));
                    } else if (NAst::TMaybeType<NAst::TSymbolType>(type)) {
                        if (width || prec) {
                            errors.push_back(TError(output->Location, "width and precision arguments are not applicable for symbol output"));
                            return node;
                        }
                        std::vector<NAst::TExprPtr> args;
                        args.push_back(arg.Expr);
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, "output_symbol"),
                            std::move(args));
                    } else if (auto named = NAst::TMaybeType<NAst::TNamedType>(type)) {
                        if (width || prec) {
                            errors.push_back(TError(output->Location, "width and precision arguments are not applicable for named type output"));
                            return node;
                        }
                        auto op = context.GetUnaryOp("вывод", type);
                        if (!op) {
                            errors.push_back(TError(arg.Expr->Location, "тип '" + named.Cast()->Name + "' не поддерживает вывод"));
                            return node;
                        }
                        call = std::make_shared<NAst::TCallExpr>(
                            output->Location,
                            std::make_shared<NAst::TIdentExpr>(output->Location, op->SynthName),
                            std::vector<NAst::TExprPtr>{ arg.Expr });
                    } else {
                        // need annotation pass to know types
                        return node;
                    }
                    stmts.push_back(call);
                }

                if (hasFileArg) {
                    // reset file after output
                    auto resetFileCall = std::make_shared<NAst::TCallExpr>(
                        output->Location,
                        std::make_shared<NAst::TIdentExpr>(output->Location, "output_reset_file"),
                        std::vector<NAst::TExprPtr>{});
                    stmts.push_back(resetFileCall);
                }

                return std::make_shared<NAst::TSeqExpr>(output->Location, stmts);
            } else if (auto maybeInput = NAst::TMaybeNode<NAst::TInputExpr>(node)) {
                auto input = maybeInput.Cast();
                if (input->Args.size() == 0) {
                    errors.push_back(TError(input->Location, "input requires at least one argument"));
                    return node;
                }
                // transform input(type) into a call to input_xxx function
                std::vector<NAst::TExprPtr> stmts;
                int i = 0;
                const auto& arg0 = input->Args[0];
                bool hasFileArg = false;
                if (NAst::TMaybeType<NAst::TFileType>(arg0->Type)) {
                    auto call = std::make_shared<NAst::TCallExpr>(
                        input->Location,
                        std::make_shared<NAst::TIdentExpr>(input->Location, "input_set_file"),
                        std::vector<NAst::TExprPtr>{arg0});
                    stmts.push_back(call);
                    hasFileArg = true;
                    i++;
                }

                for (; i < (int)input->Args.size(); ++i) {
                    const auto& arg = input->Args[i];
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
                    } else if (NAst::TMaybeType<NAst::TStringType>(type)) {
                        call = std::make_shared<NAst::TCallExpr>(
                            input->Location,
                            std::make_shared<NAst::TIdentExpr>(input->Location, "str_input"),
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
                if (hasFileArg) {
                    // reset file after input
                    auto resetFileCall = std::make_shared<NAst::TCallExpr>(
                        input->Location,
                        std::make_shared<NAst::TIdentExpr>(input->Location, "input_reset_file"),
                        std::vector<NAst::TExprPtr>{});
                    stmts.push_back(resetFileCall);
                }

                return std::make_shared<NAst::TSeqExpr>(input->Location, std::move(stmts));
            } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(node)) {
                auto index = maybeIndex.Cast();
                auto collectionType = UnwrapReferenceType(index->Collection->Type);
                if (NAst::TMaybeType<NAst::TStringType>(collectionType)) {
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
                auto collectionType = UnwrapReferenceType(slice->Collection->Type);
                if (NAst::TMaybeType<NAst::TStringType>(collectionType)) {
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
            } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(node)) {
                auto block = maybeBlock.Cast();
                for (auto stmt : block->Stmts) {
                    if (auto maybeFunDecl = NAst::TMaybeNode<NAst::TFunDecl>(stmt)) {
                        continue;
                    }
                    if (auto maybeVarDecl = NAst::TMaybeNode<NAst::TVarStmt>(stmt)) {
                        continue;
                    }
                    if (stmt->Type) {
                        auto maybeVoidType = NAst::TMaybeType<NAst::TVoidType>(stmt->Type);
                        if (!maybeVoidType) {
                            errors.push_back(TError(stmt->Location, "выражение, возвращающее результат, должно быть присвоено переменной или использовано"));
                        }
                    }
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

    auto isCallCallee = [&](const NAst::TExprPtr& target) {
        bool result = false;
        auto visit = [&](auto&& self, const NAst::TExprPtr& node) -> void {
            if (!node || result) {
                return;
            }
            if (auto maybeCall = NAst::TMaybeNode<NAst::TCallExpr>(node)) {
                auto call = maybeCall.Cast();
                if (call->Callee == target) {
                    result = true;
                    return;
                }
            }
            for (const auto& child : node->Children()) {
                self(self, child);
            }
        };
        visit(visit, expr);
        return result;
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
                    auto suggestion = context.Suggest(ident->Name, NSemantics::TScopeId{scopeId}, /*includeFunctions=*/ true);
                    auto suggestionMsg = suggestion ? suggestion->ToString() : "";
                    errors.push_back(TError(ident->Location, "Идентификатор '" + ident->Name + "' не определён." + suggestionMsg));
                    return node;
                }
                auto sym = context.GetSymbolNode(NSemantics::TSymbolId{symbolId->Id});
                if (!sym) {
                    errors.push_back(TError(ident->Location, "Некорректный идентификатор: '" + ident->Name + "'."));
                    return node;
                }
                if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(sym)) {
                    auto fun = maybeFun.Cast();
                    if (fun->Params.empty() && !isCallCallee(node)) {
                        // function call without brackets
                        auto call = std::make_shared<NAst::TCallExpr>(ident->Location, ident, std::vector<NAst::TExprPtr>{});
                        call->Type = fun->RetType;
                        return call;
                    }
                }
                return node;
            } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(node)) {
                auto block = maybeBlock.Cast();
                scopeId = block->Scope;
            } else if (auto maybeLet = NAst::TMaybeNode<NAst::TLetExpr>(node)) {
                auto letExpr = maybeLet.Cast();
                scopeId = letExpr->Scope;
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

    auto nameResolution = [&](NAst::TExprPtr& e) -> std::expected<std::monostate, TError>{
        if (auto error = PreNameResolutionTransform(e); !error) {
            return std::unexpected(error.error());
        }

        if (auto error = r.Resolve(e)) {
            return std::unexpected(*error);
        }

        if (auto error = PostNameResolutionTransform(e, r); !error) {
            return std::unexpected(error.error());
        }
        return std::monostate{};
    };

    if (auto error = nameResolution(expr); !error) {
        return std::unexpected(error.error());
    }

    NSemantics::TDefiniteAssignmentChecker definiteAssignmentChecker(r);

    NTypeAnnotation::TTypeAnnotator annotator(r);
    std::expected<bool, TError> postResult;
    int iterations = 0;

    do {
        auto annotationResult = annotator.Annotate(expr);
        if (!annotationResult) {
            return std::unexpected(annotationResult.error());
        }
        postResult = PostTypeAnnotationTransform(expr, r);
        if (!postResult) {
            return std::unexpected(postResult.error());
        }

        if (auto error = nameResolution(expr); !error) {
            return std::unexpected(error.error());
        }

        if (auto res = definiteAssignmentChecker.Check(expr); !res) {
            return std::unexpected(res.error());
        }
    } while (postResult.value() && ++iterations < MaxIterations);
    if (iterations == MaxIterations) {
        return std::unexpected(TError(expr->Location, "too many iterations in transform pipeline"));
    }

    return std::monostate{};
}

} // namespace NTransform
} // namespace NQumir
