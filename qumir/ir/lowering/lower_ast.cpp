#include "lower_ast.h"
#include "qumir/ir/builder.h"
#include "qumir/parser/parser.h"
#include "qumir/parser/type.h"

#include <iostream>
#include <sstream>
#include <cassert>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope)
{
    if (loop->PreBody == nullptr && loop->PostBody == nullptr && loop->PostCond == nullptr) {
        // while-style loop
        co_return co_await LowerWhileLoop(loop, scope);
    }

    if (loop->PreBody == nullptr && loop->PostBody == nullptr && loop->PreCond == nullptr) {
        // repeat-until style loop
        co_return co_await LowerRepeatLoop(loop, scope);
    }

    // Otherwise treat as for-style loop
    co_return co_await LowerForLoop(loop, scope);
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerWhileLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope)
{
    if (loop->PreCond == nullptr) {
        co_return TError(loop->Location, "while loop must have a condition");
    }

    auto entryId = Builder.CurrentBlockIdx();
    auto [condLabel, condId] = Builder.NewBlock();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    // Reserve end label now; place the actual end block at the end
    auto endLabel = Builder.NewLabel();

    // Jump to condition check first
    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {condLabel});

    // Condition check
    Builder.SetCurrentBlock(condId);
    auto cond = co_await Lower(loop->PreCond, scope);
    if (!cond.Value) co_return TError(loop->PreCond->Location, "while condition must be a number");
    Builder.Emit0("cmp"_op, {*cond.Value, bodyLabel, endLabel});

    // Body
    Builder.SetCurrentBlock(bodyId);
    co_await Lower(loop->Body, TBlockScope {
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = condLabel
    });
    // After body, go back to condition if body didn't already jump/return
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {condLabel});
    }

    // End: materialize the end block at the very end
    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerForLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope)
{
    // for semantics:
    // entry -> cond -> (true) -> preBody -> body -> postBody -> cond
    //                    (false) -> end
    // continue should jump to PostBody (or cond if PostBody is absent)

    if (loop->PreCond == nullptr) {
        co_return TError(loop->Location, "for loop must have a pre-condition");
    }
    if (loop->PreBody == nullptr) {
        co_return TError(loop->Location, "for loop must have a pre-body");
    }
    if (loop->PostBody == nullptr) {
        co_return TError(loop->Location, "for loop must have a post-body");
    }

    auto entryId = Builder.CurrentBlockIdx();
    auto [condLabel, condId] = Builder.NewBlock();
    auto [preLabel, preId] = Builder.NewBlock();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto [postLabel, postId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    // Jump to condition first
    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {condLabel});

    // Condition
    Builder.SetCurrentBlock(condId);
    auto cond = co_await Lower(loop->PreCond, scope);
    if (!cond.Value) co_return TError(loop->PreCond->Location, "for condition must be a number");
    // If true -> pre or body depending on PreBody presence; if false -> end
    auto trueTarget = preLabel;
    Builder.Emit0("cmp"_op, {*cond.Value, trueTarget, endLabel});

    // PreBody (if present)
    Builder.SetCurrentBlock(preId);
    auto continueTarget = postLabel;
    co_await Lower(loop->PreBody, TBlockScope{
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = continueTarget
    });

    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {bodyLabel});
    }

    // Body
    Builder.SetCurrentBlock(bodyId);
    co_await Lower(loop->Body, TBlockScope{
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = continueTarget
    });
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, { loop->PostBody ? postLabel : condLabel });
    }

    // PostBody, then back to condition
    Builder.SetCurrentBlock(postId);
    co_await Lower(loop->PostBody, TBlockScope{
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        // Inside post body, continue should proceed to condition
        .ContinueLabel = condLabel
    });

    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {condLabel});
    }

    // End block
    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerRepeatLoop(std::shared_ptr<NAst::TLoopStmtExpr> loop, TBlockScope scope)
{
    if (loop->PostCond == nullptr) {
        co_return TError(loop->Location, "repeat-until loop must have a condition");
    }

    // Create blocks for body and condition
    auto entryId = Builder.CurrentBlockIdx();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto [condLabel, condId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    // Jump to body first (do-while: body executes at least once)
    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {bodyLabel});

    // Body block
    Builder.SetCurrentBlock(bodyId);
    co_await Lower(loop->Body, TBlockScope {
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = condLabel
    });
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {condLabel});
    }

    // Condition block
    Builder.SetCurrentBlock(condId);
    auto cond = co_await Lower(loop->PostCond, scope);
    if (!cond.Value) co_return TError(loop->PostCond->Location, "repeat-until condition must be a number");
    // If condition is true, repeat; if false, exit
    Builder.Emit0("cmp"_op, {*cond.Value, bodyLabel, endLabel});

    // End block
    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}


TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::Lower(const NAst::TExprPtr& expr, TBlockScope scope) {
    if (auto maybeNum = NAst::TMaybeNode<NAst::TNumberExpr>(expr)) {
        auto num = maybeNum.Cast();
        if (num->IsFloat) {
            co_return TValueWithBlock{ TImm{.Value = std::bit_cast<int64_t>(num->FloatValue), .IsFloat = true}, Builder.CurrentBlockLabel() };
        } else {
            co_return TValueWithBlock{ TImm{.Value = num->IntValue, .IsFloat = false}, Builder.CurrentBlockLabel() };
        }
    } else if (auto maybeStringLiteral = NAst::TMaybeNode<NAst::TStringLiteralExpr>(expr)) {
        auto str = maybeStringLiteral.Cast();
        auto id = Builder.StringLiteral(str->Value);
        // TODO: type is 'pointer to char'
        co_return TValueWithBlock{ TImm{.Value = id, .IsFloat = false}, Builder.CurrentBlockLabel() };
    } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(expr)) {
        // Evaluate a block: value is the value of the last statement (or void if none)
        std::optional<TOperand> last;
        auto block = maybeBlock.Cast();
        auto newScope = scope;
        newScope.Id = NSemantics::TScopeId{block->Scope};
        for (auto& s : block->Stmts) {
            auto r = co_await Lower(s, newScope);
            last = r.Value;
        }
        // TODO: return only if function block and last is 'return'
        co_return TValueWithBlock{ last, Builder.CurrentBlockLabel() };
    } else if (auto maybeUnary = NAst::TMaybeNode<NAst::TUnaryExpr>(expr)) {
        auto unary = maybeUnary.Cast();
        auto operand = co_await Lower(unary->Operand, scope);
        if (!operand.Value) co_return TError(unary->Operand->Location, "operand of unary must be a number");
        if (unary->Operator == '-') {
            auto tmp = Builder.Emit1("neg"_op, {*operand.Value});
            Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
            co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
        } else if (unary->Operator == '!'_op) {
            auto tmp = Builder.Emit1("!"_op, {*operand.Value});
            Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
            co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
        }
        co_return TValueWithBlock{ operand.Value, operand.ProducingLabel };
    } else if (auto maybeBinary = NAst::TMaybeNode<NAst::TBinaryExpr>(expr)) {
        auto binary = maybeBinary.Cast();
        bool isLazy = (binary->Operator == "&&"_op || binary->Operator == "||"_op);
        auto leftRes = co_await Lower(binary->Left, scope);
        auto leftNum = leftRes.Value;

        std::optional<TOperand> rightNum;
        if (!isLazy) {
            auto rres = co_await Lower(binary->Right, scope);
            rightNum = rres.Value;
        }
        if (!leftNum) co_return TError(binary->Location, "binary operands must be numbers");
        if (!isLazy && !rightNum) co_return TError(binary->Location, "binary operands must be numbers");

        switch ((uint64_t)binary->Operator) {
            case "&&"_op: {
                // Short-circuit AND using single end block + phi2 with real producing blocks.
                // Evaluate left expression first (already done: leftNum) and capture its producing block.
                auto leftProducingLabel = leftRes.ProducingLabel; // block where left value was produced

                // Normalize left operand to a register if it's an immediate.
                if (leftNum->Type == TOperand::EType::Imm) {
                    *leftNum = Builder.Emit1("cmov"_op, {*leftNum});
                    Builder.SetType(leftNum->Tmp, FromAstType(expr->Type, Module.Types));
                }

                // Create blocks for RHS evaluation and the end/merge.
                auto [rhsLabel, rhsId] = Builder.NewBlock();
                auto endLabel = Builder.NewLabel();

                // Emit branch on left: if true -> rhs; if false -> end.
                Builder.SetCurrentBlock(leftProducingLabel);
                Builder.Emit0("cmp"_op, {*leftNum, rhsLabel, endLabel});
                auto leftEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left is false

                // RHS path
                Builder.SetCurrentBlock(rhsId);
                auto r = co_await Lower(binary->Right, scope);
                if (!r.Value) co_return TError(binary->Right->Location, "binary operands must be numbers");
                if (r.Value->Type == TOperand::EType::Imm) {
                    *r.Value = Builder.Emit1("cmov"_op, {*r.Value});
                    Builder.SetType(r.Value->Tmp, FromAstType(expr->Type, Module.Types));
                }
                // Record truth of RHS (both edges go to end)
                Builder.Emit0("jmp"_op, {endLabel});
                auto rightEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was true

                // End/merge block and phi2 selecting left(on false) vs right(on true)
                Builder.NewBlock(endLabel);
                auto res = Builder.Emit1("phi2"_op, {*leftNum, leftEdgeLabel, *r.Value, rightEdgeLabel});
                Builder.SetType(res, FromAstType(expr->Type, Module.Types));
                Builder.UnifyTypes(res, leftNum->Tmp);
                Builder.UnifyTypes(res, r.Value->Tmp);
                co_return TValueWithBlock{ res, Builder.CurrentBlockLabel() };
            }
            case "||"_op: {
                // Short-circuit OR using single end block + phi2 with real producing blocks.
                auto leftProducingLabel = leftRes.ProducingLabel;
                if (leftNum->Type == TOperand::EType::Imm) {
                    *leftNum = Builder.Emit1("cmov"_op, {*leftNum});
                    Builder.SetType(leftNum->Tmp, FromAstType(expr->Type, Module.Types));
                }

                auto [rhsLabel, rhsId] = Builder.NewBlock();
                auto endLabel = Builder.NewLabel();

                // If left is true -> end; else -> rhs
                Builder.SetCurrentBlock(leftProducingLabel);
                Builder.Emit0("cmp"_op, {*leftNum, endLabel, rhsLabel});
                auto leftEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was true

                Builder.SetCurrentBlock(rhsId);
                auto r = co_await Lower(binary->Right, scope);
                if (!r.Value) co_return TError(binary->Right->Location, "binary operands must be numbers");
                if (r.Value->Type == TOperand::EType::Imm) {
                    *r.Value = Builder.Emit1("cmov"_op, {*r.Value});
                    Builder.SetType(r.Value->Tmp, FromAstType(expr->Type, Module.Types));
                }
                Builder.Emit0("jmp"_op, {endLabel});
                auto rightEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was false

                Builder.NewBlock(endLabel);
                Builder.UnifyTypes(leftNum->Tmp, r.Value->Tmp);
                auto res = Builder.Emit1("phi2"_op, {*leftNum, leftEdgeLabel, *r.Value, rightEdgeLabel});
                Builder.SetType(res, FromAstType(expr->Type, Module.Types));
                Builder.UnifyTypes(res, leftNum->Tmp);
                Builder.UnifyTypes(res, r.Value->Tmp);
                co_return TValueWithBlock{ res, Builder.CurrentBlockLabel() };
            }
            default:
                {
                    if (leftNum->Type == TOperand::EType::Tmp && rightNum->Type == TOperand::EType::Tmp) {
                        auto leftType = Builder.GetType(leftNum->Tmp);
                        auto rightType = Builder.GetType(rightNum->Tmp);
                        if (leftType != rightType) {
                            // unify types
                            auto unified = Module.Types.Unify(leftType, rightType);
                            // tf64 : int -> float64
                            // tf32 : int -> float32
                            if (!Module.Types.IsFloat(leftType) && Module.Types.IsFloat(unified)) {
                                auto casted = Builder.Emit1("i2f"_op, {*leftNum});
                                Builder.SetType(casted, unified);
                                leftNum = casted;
                            }
                            if (!Module.Types.IsFloat(rightType) && Module.Types.IsFloat(unified)) {
                                auto casted = Builder.Emit1("i2f"_op, {*rightNum});
                                Builder.SetType(casted, unified);
                                rightNum = casted;
                            }
                        }
                    }
                    auto tmp = Builder.Emit1((uint32_t)binary->Operator.Value /* ast op to ir op mapping */, {*leftNum, *rightNum});
                    Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
                    co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
                }
        }
    } else if (auto maybeIfe = NAst::TMaybeNode<NAst::TIfExpr>(expr)) {
        // If is a statement in this language: no result value, no phi merge.
        auto ife = maybeIfe.Cast();
        auto cond = co_await Lower(ife->Cond, scope);
        if (!cond.Value) co_return TError(ife->Cond->Location, "if condition must be a number");

        auto entryId = Builder.CurrentBlockIdx();
        auto [thenLabel, thenId] = Builder.NewBlock();
        auto [elseLabel, elseId] = Builder.NewBlock();
        auto endLabel = Builder.NewLabel();

        // Branch on condition
        Builder.SetCurrentBlock(entryId);
        Builder.Emit0("cmp"_op, {*cond.Value, thenLabel, elseLabel});

        // Then branch
        Builder.SetCurrentBlock(thenId);
        co_await Lower(ife->Then, scope);
        if (!Builder.IsCurrentBlockTerminated()) {
            Builder.Emit0("jmp"_op, {endLabel});
        }

        // Else branch (may be present). If absent, just jump to end.
        Builder.SetCurrentBlock(elseId);
        if (ife->Else) {
            co_await Lower(ife->Else, scope);
        }
        if (!Builder.IsCurrentBlockTerminated()) {
            Builder.Emit0("jmp"_op, {endLabel});
        }

        // End/merge block without phi, as if is a statement
        Builder.NewBlock(endLabel);
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };

    } else if (auto maybeLoop = NAst::TMaybeNode<NAst::TLoopStmtExpr>(expr)) {
        auto loop = maybeLoop.Cast();
        co_return co_await LowerLoop(loop, scope);
    } else if (NAst::TMaybeNode<NAst::TBreakStmt>(expr)) {
        if (!scope.BreakLabel) {
            co_return TError(expr->Location, "break not in a loop");
        }
        // terminate current block by jumping to the break target
        Builder.Emit0("jmp"_op, {*scope.BreakLabel});
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (NAst::TMaybeNode<NAst::TContinueStmt>(expr)) {
        if (!scope.ContinueLabel) {
            co_return TError(expr->Location, "continue not in a loop");
        }
        // terminate current block by jumping to the continue target
        Builder.Emit0("jmp"_op, {*scope.ContinueLabel});
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TAssignExpr>(expr)) {
        auto asg = maybeAsg.Cast();
        auto rhs = co_await Lower(asg->Value, scope);
        if (!rhs.Value) co_return TError(asg->Value->Location, "right-hand side of assignment must be a number");
        auto sidOpt = Context.Lookup(asg->Name, scope.Id);
        if (!sidOpt) co_return TError(asg->Location, "assignment to undefined");

        auto storeSlot = TSlot{sidOpt->Id};
        auto node = Context.GetSymbolNode(*sidOpt);
        auto slotType = FromAstType(node->Type, Module.Types);
        Builder.SetType(storeSlot, slotType);

        if (rhs.Value->Type == TOperand::EType::Imm) {
            // cast immediate to a register before storing (for cast)
            if (rhs.Value->Imm.IsFloat && Module.Types.IsInteger(slotType)) {
                *rhs.Value = Builder.Emit1("cmov"_op, {*rhs.Value});
                Builder.SetType(rhs.Value->Tmp, FromAstType(expr->Type, Module.Types));
            } else if (rhs.Value->Imm.IsFloat == false && Module.Types.IsFloat(slotType)) {
                *rhs.Value = Builder.Emit1("i2f"_op, {*rhs.Value});
                Builder.SetType(rhs.Value->Tmp, FromAstType(expr->Type, Module.Types));
            }
        }
        Builder.Emit0("stre"_op, {storeSlot, *rhs.Value});
        // store does not produce a value
        co_return TValueWithBlock{ {}, Builder.CurrentBlockLabel() };
    } else if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(expr)) {
        auto ident = maybeIdent.Cast();
        auto sidOpt = Context.Lookup(ident->Name, scope.Id);
        if (!sidOpt) co_return TError(ident->Location, std::string("undefined name: ") + ident->Name);

        auto loadSlot = TSlot{sidOpt->Id};
        auto node = Context.GetSymbolNode(*sidOpt);
        // we don't set type of loadSlot here, as it was typed on store
        auto tmp = Builder.Emit1("load"_op, {loadSlot});
        Builder.SetType(tmp, FromAstType(node->Type, Module.Types));
        co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
    } else if (NAst::TMaybeNode<NAst::TVarStmt>(expr)) {
        // do nothing
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(expr)) {
        auto fun = maybeFun.Cast();
        auto name = fun->Name;
        if (scope.Id.Id != 0) {
            // Nested function: qualify name with scope id to avoid collisions
            name += "@s" + std::to_string(scope.Id.Id);
        }
        auto params = fun->Params;
        auto body = fun->Body;
        auto type = NAst::TMaybeType<NAst::TFunctionType>(fun->Type).Cast();
        // Explicit symbol lookup (synchronous) instead of co_await on optionals.
        // Prefer direct decl binding (robust for nested functions where scope.Id may not be the decl scope).
        auto sidOpt = Context.Lookup(fun->Name, scope.Id);
        if (!sidOpt) {
            co_return TError(fun->Location, std::string("unbound function symbol '") + fun->Name + "' in scope " + std::to_string(scope.Id.Id) );
        }
        auto funScope = fun->Body->Scope;
        std::vector<TSlot> paramSlots; paramSlots.reserve(params.size());
        int i = 0;
        for (auto& p : params) {
            auto psid = Context.Lookup(p->Name, NSemantics::TScopeId{funScope});
            if (!psid) {
                co_return TError(p->Location, "parameter has no binding");
            }
            auto slot = TSlot{ psid->Id };
            if (type) {
                Builder.SetType(slot, FromAstType(type->ParamTypes[i], Module.Types));
            }
            paramSlots.push_back(slot);
            i++;
        }

        auto currentFuncIdx = Builder.CurrentFunctionIdx();
        auto funcIdx = Builder.NewFunction(name, paramSlots, sidOpt->Id);
        Builder.SetReturnType(FromAstType(fun->RetType, Module.Types));

        auto loweredBody = co_await Lower(body, TBlockScope {
            .FuncIdx = funcIdx,
            .Id = NSemantics::TScopeId{funScope},
            .BreakLabel = std::nullopt,
            .ContinueLabel = std::nullopt
        });
        if (loweredBody.Value) {
            // TODO: return value of 'hidden' __return variable
            Builder.Emit0("ret"_op, {*loweredBody.Value});
        } else {
            Builder.Emit0("ret"_op, {});
        }
        Builder.SetCurrentFunction(currentFuncIdx);
        // Function declaration does not produce a value
        co_return TValueWithBlock{ {}, Builder.CurrentBlockLabel() };
    } else if (auto maybeCall = NAst::TMaybeNode<NAst::TCallExpr>(expr)) {
        auto call = maybeCall.Cast();
        // Evaluate callee and perform a function call.

        int32_t calleeSymId = -1;
        std::string calleeName;
        if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(call->Callee)) {
            auto ident = maybeIdent.Cast();
            auto sidOpt = Context.Lookup(ident->Name, scope.Id);
            calleeName = ident->Name;
            if (!sidOpt) co_return TError(ident->Location, std::string("undefined function: ") + ident->Name);
            calleeSymId = sidOpt->Id;
        } else {
            co_return TError(call->Callee->Location, "function call to non-identifier not supported");
        }

        auto funDecl = NAst::TMaybeNode<NAst::TFunDecl>(Context.GetSymbolNode(NSemantics::TSymbolId{calleeSymId})).Cast();
        if (!funDecl) {
            co_return TError(call->Callee->Location, "not a function");
        }
        NAst::TTypePtr returnType = funDecl->RetType;
        std::vector<NAst::TTypePtr>* argTypes = nullptr;
        if (auto maybeFuncType = NAst::TMaybeType<NAst::TFunctionType>(funDecl->Type)) {
            argTypes = &maybeFuncType.Cast()->ParamTypes;
        }

        std::vector<TOperand> argv;
        argv.reserve(call->Args.size());
        int i = 0;
        for (auto& a : call->Args) {
            auto av = co_await Lower(a, scope);
            if (!av.Value) co_return TError(a->Location, "invalid argument");
            if (argTypes) {
                const auto& argType = (*argTypes)[i++];
                if (av.Value->Type == TOperand::EType::Imm) {
                    auto maybeFloat = NAst::TMaybeType<NAst::TFloatType>(argType);
                    if (maybeFloat && av.Value->Imm.IsFloat == false) {
                        // Argument is a float but was lowered to int: convert
                        double tmp = static_cast<double>(av.Value->Imm.Value);
                        av.Value = TImm{.Value = std::bit_cast<int64_t>(tmp), .IsFloat = true};
                    }
                }
            } else {
                // untyped arg: accept as-is (unit tests)
            }
            argv.push_back(*av.Value);
        }
        for (auto arg : argv) {
            Builder.Emit0("arg"_op, {arg});
        }
        // Decide if callee returns a value (non-void). If void -> Emit0
        bool returnsValue = false;
        if (NAst::TMaybeType<NAst::TVoidType>(returnType)) {
            returnsValue = false;
        } else {
            returnsValue = true;
        }

        if (returnsValue) {
            auto tmp = Builder.Emit1("call"_op, {TImm{calleeSymId}});
            Builder.SetType(tmp, FromAstType(returnType, Module.Types));
            co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
        } else {
            Builder.Emit0("call"_op, {TImm{calleeSymId}});
            co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
        }
    } else if (auto maybeOutput = NAst::TMaybeNode<NAst::TOutputExpr>(expr)) {
        auto output = maybeOutput.Cast();
        for (auto& arg : output->Args) {
            auto av = co_await Lower(arg, scope);
            if (!av.Value) co_return TError(arg->Location, "invalid argument to output");
            auto type = arg->Type;
            if (NAst::TMaybeType<NAst::TFloatType>(type)) {
                Builder.Emit0("outf"_op, {*av.Value});
            } else if (NAst::TMaybeType<NAst::TIntegerType>(type)) {
                Builder.Emit0("outi"_op, {*av.Value});
            } else if (NAst::TMaybeType<NAst::TStringType>(type)) {
                Builder.Emit0("outs"_op, {*av.Value});
            } else {
                co_return TError(arg->Location, "output argument must be int, float, or string");
            }
        }
        // output has no value
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeInput = NAst::TMaybeNode<NAst::TInputExpr>(expr)) {
        auto input = maybeInput.Cast();
        for (auto& arg : input->Args) {
            // arg must be a slot, resolve it
            auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(arg);
            if (!maybeIdent) {
                co_return TError(arg->Location, "input should be a var");
            }
            auto ident = maybeIdent.Cast();
            auto sidOpt = Context.Lookup(ident->Name, scope.Id);
            if (!sidOpt) {
                co_return TError(arg->Location, "assignment to undefined");
            }
            auto storeSlot = TSlot{sidOpt->Id};

            auto type = arg->Type;
            if (NAst::TMaybeType<NAst::TFloatType>(type)) {
                auto tmp = Builder.Emit1("inf"_op, {});
                Builder.Emit0("stre", {storeSlot, tmp});
            } else if (NAst::TMaybeType<NAst::TIntegerType>(type)) {
                auto tmp = Builder.Emit1("ini"_op, {});
                Builder.Emit0("stre", {storeSlot, tmp});
            } else {
                co_return TError(arg->Location, "output argument must be int, float");
            }
        }
        // output has no value
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else {
        std::ostringstream oss;
        oss << *expr;
        co_return TError(expr->Location,
            std::string("not implemented: lowering for this AST node: ") + oss.str());
    }
}

void TAstLowerer::ImportExternalFunction(int symbolId, const NAst::TFunDecl& funcDecl) {
    if (Module.SymIdToExtFuncIdx.find(symbolId) != Module.SymIdToExtFuncIdx.end()) {
        // already imported
        return;
    }

    std::vector<EKind> argTypes;
    std::optional<EKind> returnType;

    TExternalFunction func {
        .Name = funcDecl.Name,
        .MangledName = funcDecl.MangledName,
        .Addr = funcDecl.Ptr,
        .Packed = funcDecl.Packed,
        .SymId = symbolId
    };
    int funIdx = Module.ExternalFunctions.size();
    Module.ExternalFunctions.push_back(func);
    Module.SymIdToExtFuncIdx[symbolId] = funIdx;
}

void TAstLowerer::ImportExternalFunctions() {
    for (const auto& [symbolId, func] : Context.GetExternalFunctions()) {
        ImportExternalFunction(symbolId, *func);
    }
}

std::expected<TFunction*, TError> TAstLowerer::LowerTop(const NAst::TExprPtr& expr) {
    ImportExternalFunctions();

    std::string funcName = std::string("__init") + std::to_string(NextReplChunk++);
    auto symbolId = Context.DeclareFunction(
        funcName,
        expr);

    expr->Type = std::make_shared<NAst::TVoidType>();

    auto idx = Builder.NewFunction(funcName, {}, symbolId.Id);
    Builder.SetReturnType(FromAstType(expr->Type, Module.Types));
    const auto& result = Lower(expr, TBlockScope {
        .FuncIdx = idx,
        .Id = NSemantics::TScopeId{0}
    }).result();
    if (!result) {
        return std::unexpected(result.error());
    }
    auto maybeOperand = result.value().Value;
    if (maybeOperand) {
        Builder.Emit0(TOp("ret"), {*maybeOperand});
    } else {
        Builder.Emit0("ret"_op, {});
    }
    return &Module.Functions[idx];
}


} // namespace NIR
} // namespace NQumir
