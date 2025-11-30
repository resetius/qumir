#include "lower_ast.h"
#include "qumir/ir/builder.h"
#include "qumir/parser/parser.h"
#include "qumir/parser/type.h"

#include <iostream>
#include <sstream>
#include <cassert>
#include <functional>

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
        Builder.Emit0("jmp"_op, { postLabel });
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

TExpectedTask<TTmp, TError, TLocation> TAstLowerer::LoadVar(const std::string& name, TBlockScope scope, const TLocation& loc, bool takeRefOfNotRef)
{
    auto var = Context.Lookup(name, scope.Id);
    if (!var) {
        co_return TError(loc, "undefined variable: `" + name + "'");
    }

    auto node = Context.GetSymbolNode(NSemantics::TSymbolId{var->Id});
    if (!node) {
        co_return TError(loc, "undefined variable: `" + name + "'");
    }
    if (NAst::TMaybeType<NAst::TReferenceType>(node->Type) && takeRefOfNotRef) {
        takeRefOfNotRef = false;
    }

    TOperand op = (var->FunctionLevelIdx >= 0)
        ? TOperand{ TLocal{ var->FunctionLevelIdx } }
        : TOperand{ TSlot{ var->Id } };

    TOp opcode = takeRefOfNotRef ? "lea"_op : "load"_op;
    auto tmp = Builder.Emit1(opcode, { op });
    Builder.SetType(tmp, FromAstType(node->Type, Module.Types));
    co_return tmp;
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerIndices(const std::string& name, const std::vector<NAst::TExprPtr>& indices, TBlockScope scope)
{
    int n = indices.size() - 1;
    int i = n;
    auto i64 = Module.Types.I(EKind::I64);

    std::optional<TTmp> prev;

    for (; i >= 0; --i) {
        auto indexRes = co_await Lower(indices[i], scope);
        if (!indexRes.Value) {
            co_return TError(indices[i]->Location, "array index must be a number");
        }
        // totalIndex = sum (index - lowerBound) * stride_i
        // stride_n = 1
        // stride_{n-1} = mulAccName_{n}

        auto tmp = co_await LoadVar("$$" + name + "_lbound" + std::to_string(i), scope, indices[i]->Location);
        tmp = Builder.Emit1("-"_op, {*indexRes.Value, tmp});
        Builder.SetType(tmp, i64);
        if (i != n) {
            auto stride = co_await LoadVar("$$" + name + "_mulacc" + std::to_string(i+1), scope, indices[i]->Location);
            tmp = Builder.Emit1("*"_op, {tmp, stride});
            Builder.SetType(tmp, i64);
        }

        if (prev) {
            tmp = Builder.Emit1("+"_op, {tmp, *prev});
            Builder.SetType(tmp, i64);
        }

        prev = tmp;
    }
    *prev = Builder.Emit1("*"_op, {*prev, TImm{8, i64}}); // TODO: element size
    Builder.SetType(*prev, i64);
    co_return TValueWithBlock{ *prev, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::Lower(const NAst::TExprPtr& expr, TBlockScope scope) {
    int lowStringTypeId = Module.Types.Ptr(Module.Types.I(EKind::I8));

    if (auto maybeCast = NAst::TMaybeNode<NAst::TCastExpr>(expr)) {
        auto cast = maybeCast.Cast();
        auto operand = co_await Lower(cast->Operand, scope);
        if (!operand.Value) co_return TError(cast->Operand->Location, "operand of cast must be a value");
        std::optional<TOperand> tmp;
        if (NAst::TMaybeType<NAst::TIntegerType>(expr->Type) && NAst::TMaybeType<NAst::TFloatType>(cast->Operand->Type)) {
            // float to int cast
            tmp = Builder.Emit1("f2i"_op, {*operand.Value});
        } else if (NAst::TMaybeType<NAst::TFloatType>(expr->Type) && NAst::TMaybeType<NAst::TIntegerType>(cast->Operand->Type)) {
            // int to float cast
            tmp = Builder.Emit1("i2f"_op, {*operand.Value});
        } else if (NAst::TMaybeType<NAst::TBoolType>(expr->Type) && NAst::TMaybeType<NAst::TIntegerType>(cast->Operand->Type)) {
            tmp = Builder.Emit1("i2b"_op, {*operand.Value});
        } else if (NAst::TMaybeType<NAst::TBoolType>(expr->Type) && NAst::TMaybeType<NAst::TFloatType>(cast->Operand->Type)) {
            tmp = Builder.Emit1("f2b"_op, {*operand.Value});
        } else if (NAst::TMaybeType<NAst::TSymbolType>(expr->Type) && NAst::TMaybeType<NAst::TIntegerType>(cast->Operand->Type)) {
            tmp = Builder.Emit1("mov"_op, {*operand.Value});
        } else if (NAst::TMaybeType<NAst::TIntegerType>(expr->Type) && NAst::TMaybeType<NAst::TSymbolType>(cast->Operand->Type)) {
            // oposite of above: int to symbol
            tmp = Builder.Emit1("mov"_op, {*operand.Value});
        } else {
            co_return TError(cast->Location, "unsupported cast types: from " + std::string(cast->Operand->Type->TypeName()) + " to " + std::string(expr->Type->TypeName()));
        }
        Builder.SetType(tmp->Tmp, FromAstType(expr->Type, Module.Types));
        co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
    } else if (auto maybeNum = NAst::TMaybeNode<NAst::TNumberExpr>(expr)) {
        auto num = maybeNum.Cast();
        if (num->IsFloat) {
            co_return TValueWithBlock{ TImm{.Value = std::bit_cast<int64_t>(num->FloatValue), .TypeId = Module.Types.I(EKind::F64)}, Builder.CurrentBlockLabel() };
        } else {
            co_return TValueWithBlock{ TImm{.Value = num->IntValue, .TypeId = Module.Types.I(EKind::I64)}, Builder.CurrentBlockLabel() };
        }
    } else if (auto maybeStringLiteral = NAst::TMaybeNode<NAst::TStringLiteralExpr>(expr)) {
        auto str = maybeStringLiteral.Cast();
        auto id = Builder.StringLiteral(str->Value);
        // TODO: type is 'pointer to char'
        co_return TValueWithBlock{ TImm{.Value = id, .TypeId = Module.Types.Ptr(Module.Types.I(EKind::I8))}, Builder.CurrentBlockLabel() };
    } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(expr)) {
        // Evaluate a block: value is the value of the last statement (or void if none)
        std::optional<TOperand> last;
        auto block = maybeBlock.Cast();
        auto newScope = scope;
        newScope.Id = NSemantics::TScopeId{block->Scope};

        // Track scope for destructors
        size_t initialPendingDestructorsSize = PendingDestructors.size();
        for (auto& s : block->Stmts) {
            auto r = co_await Lower(s, newScope);
            last = r.Value;

            if (r.Ownership == EOwnership::Owned) {
                last = std::nullopt;
                auto dtorId = co_await GlobalSymbolId("str_release");
                Builder.Emit0("arg"_op, {*r.Value});
                Builder.Emit0("call"_op, { TImm{ dtorId } });
                last = r.Value;
            }

            // Stop lowering subsequent statements if current block has a terminating instruction (e.g. break -> jmp)
            if (Builder.IsCurrentBlockTerminated()) {
                break;
            }
        }
        // Emit destructors for strings declared in this block (LIFO)
        if (!block->SkipDestructors && PendingDestructors.size() > initialPendingDestructorsSize) {
            // Release in reverse order of declaration
            for (size_t i = PendingDestructors.size(); i-- > initialPendingDestructorsSize; ) {
                auto& dtor = PendingDestructors[i];
                // Load current value of the local (or slot) and call str_release(val)
                for (size_t i = 0; i < dtor.Args.size(); ++i) {
                    auto& operand = dtor.Args[i];
                    if (operand.Type == TOperand::EType::Local || operand.Type == TOperand::EType::Slot) {
                        TTmp val = Builder.Emit1("load"_op, { operand });
                        auto typeId = dtor.TypeIds[i];
                        if (typeId >= 0) {
                            Builder.SetType(val, typeId);
                        }
                        operand = val;
                    }
                }
                for (auto& operand : dtor.Args) {
                    Builder.Emit0("arg"_op, { operand });
                }
                Builder.Emit0("call"_op, { TImm{ dtor.FunctionId } });
            }
            // Remove destructors belonging to this block
            PendingDestructors.resize(initialPendingDestructorsSize);
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

        decltype(leftNum) rightNum;
        decltype(leftRes) rightRes;
        if (!isLazy) {
            rightRes = co_await Lower(binary->Right, scope);
            rightNum = rightRes.Value;
        }
        if (!leftNum) co_return TError(binary->Location, "binary operands must be numbers");
        if (!isLazy && !rightNum) co_return TError(binary->Location, "binary operands must be numbers");

        switch ((uint64_t)binary->Operator) {
            case "&&"_op: {
                // Short-circuit AND using single end block + phi2 with real producing blocks.
                // Evaluate left expression first (already done: leftNum) and capture its producing block.
                auto leftProducingLabel = leftRes.ProducingLabel; // block where left value was produced

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
                // Record truth of RHS (both edges go to end)
                Builder.Emit0("jmp"_op, {endLabel});
                auto rightEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was true

                // End/merge block and phi2 selecting left(on false) vs right(on true)
                Builder.NewBlock(endLabel);
                auto res = Builder.Emit1("phi"_op, {*leftNum, leftEdgeLabel, *r.Value, rightEdgeLabel});
                Builder.SetType(res, FromAstType(expr->Type, Module.Types));
                Builder.UnifyTypes(res, leftNum->Tmp);
                Builder.UnifyTypes(res, r.Value->Tmp);
                co_return TValueWithBlock{ res, Builder.CurrentBlockLabel() };
            }
            case "||"_op: {
                // Short-circuit OR using single end block + phi2 with real producing blocks.
                auto leftProducingLabel = leftRes.ProducingLabel;
                auto [rhsLabel, rhsId] = Builder.NewBlock();
                auto endLabel = Builder.NewLabel();

                // If left is true -> end; else -> rhs
                Builder.SetCurrentBlock(leftProducingLabel);
                Builder.Emit0("cmp"_op, {*leftNum, endLabel, rhsLabel});
                auto leftEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was true

                Builder.SetCurrentBlock(rhsId);
                auto r = co_await Lower(binary->Right, scope);
                if (!r.Value) co_return TError(binary->Right->Location, "binary operands must be numbers");
                Builder.Emit0("jmp"_op, {endLabel});
                auto rightEdgeLabel = Builder.CurrentBlockLabel(); // predecessor into end when left was false

                Builder.NewBlock(endLabel);
                Builder.UnifyTypes(leftNum->Tmp, r.Value->Tmp);
                auto res = Builder.Emit1("phi"_op, {*leftNum, leftEdgeLabel, *r.Value, rightEdgeLabel});
                Builder.SetType(res, FromAstType(expr->Type, Module.Types));
                Builder.UnifyTypes(res, leftNum->Tmp);
                Builder.UnifyTypes(res, r.Value->Tmp);
                co_return TValueWithBlock{ res, Builder.CurrentBlockLabel() };
            }
            default:
                {
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
    } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TArrayAssignExpr>(expr)) {
        auto asg = maybeAsg.Cast();

        auto indices = co_await LowerIndices(asg->Name, asg->Indices, scope);
        if (!indices.Value) {
            co_return TError(asg->Location, "failed to lower array indices");
        }
        auto totalIndex = *indices.Value;

        auto arrayPtr = co_await LoadVar(asg->Name, scope, asg->Location);
        auto arrayType = Builder.GetType(arrayPtr);
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, totalIndex});
        Builder.SetType(destPtr, arrayType);

        auto rhs = co_await Lower(asg->Value, scope);
        if (!rhs.Value) co_return TError(asg->Value->Location, "right-hand side of assignment must be a number");

        int arrayElemType = Module.Types.UnderlyingType(arrayType);
        // copy-paste
        if (rhs.Value->Type == TOperand::EType::Imm) {
            // Materialize immediate string literals into a tmp
            if (rhs.Value->Imm.TypeId == lowStringTypeId) {
                // TODO: create proper kind for string literal
                auto constructorId = co_await GlobalSymbolId("str_from_lit");
                Builder.Emit0("arg"_op, {*rhs.Value});
                auto materializedString = Builder.Emit1("call"_op, {TImm{constructorId}});
                Builder.SetType(materializedString, arrayElemType);
                *rhs.Value = materializedString;
                rhs.Ownership = EOwnership::Owned;
            }
        }

        // copy-paste
        if (arrayElemType == lowStringTypeId && rhs.Ownership == EOwnership::Borrowed) {
            auto retainId = co_await GlobalSymbolId("str_retain");
            Builder.Emit0("arg"_op, {*rhs.Value});
            Builder.Emit0("call"_op, { TImm{ retainId } });
        }

        // copy-paste
        if (arrayElemType == lowStringTypeId) {
            auto dtorId = co_await GlobalSymbolId("str_release");
            auto existingVal = Builder.Emit1("lde"_op, { destPtr });
            Builder.SetType(existingVal, arrayElemType);
            Builder.Emit0("arg"_op, { existingVal });
            Builder.Emit0("call"_op, { TImm{ dtorId } });
        }

        Builder.Emit0("ste"_op, {destPtr, *rhs.Value});
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(expr)) {
        auto index = maybeIndex.Cast();
        auto indexValue = co_await Lower(index->Index, scope);
        if (!indexValue.Value) {
            co_return TError(index->Index->Location, "array index must be a number");
        }
        auto value = co_await Lower(index->Collection, scope);
        if (!value.Value) {
            co_return TError(index->Collection->Location, "failed to lower collection");
        }
        auto arrayPtr = *value.Value;
        if (arrayPtr.Type != TOperand::EType::Tmp) {
            co_return TError(index->Collection->Location, "collection is not an array");
        }

        // Adjust index by lower bound: index0 = index - lbound0
        auto lbound0 = co_await LoadVar("$$" + NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection).Cast()->Name + "_lbound0", scope, index->Index->Location);
        auto i64 = Module.Types.I(EKind::I64);
        auto zeroBasedIndex = Builder.Emit1("-"_op, {*indexValue.Value, lbound0});
        Builder.SetType(zeroBasedIndex, i64);

        auto arrayType = Builder.GetType(arrayPtr.Tmp);
        auto offset = Builder.Emit1("*"_op, {zeroBasedIndex, TImm{8, i64}}); // TODO: element size
        Builder.SetType(offset, i64);
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, offset});
        Builder.SetType(destPtr, arrayType);
        auto loaded = Builder.Emit1("lde"_op, { destPtr });
        Builder.SetType(loaded, FromAstType(expr->Type, Module.Types));
        co_return TValueWithBlock{ loaded, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
    } else if (auto maybeMultiIndex = NAst::TMaybeNode<NAst::TMultiIndexExpr>(expr)) {
        auto multiIndex = maybeMultiIndex.Cast();
        auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(multiIndex->Collection);
        if (!maybeIdent) {
            co_return TError(multiIndex->Collection->Location, "multi-index collection must be an identifier");
        }
        auto asg = maybeIdent.Cast();
        auto indices = co_await LowerIndices(asg->Name, multiIndex->Indices, scope);
        if (!indices.Value) {
            co_return TError(asg->Location, "failed to lower array indices");
        }
        auto totalIndex = *indices.Value;
        auto value = co_await Lower(multiIndex->Collection, scope);
        if (!value.Value) {
            co_return TError(multiIndex->Collection->Location, "failed to lower collection");
        }
        auto arrayPtr = *value.Value;
        if (arrayPtr.Type != TOperand::EType::Tmp) {
            co_return TError(multiIndex->Collection->Location, "collection is not an array");
        }
        auto arrayType = Builder.GetType(arrayPtr.Tmp);
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, totalIndex});
        Builder.SetType(destPtr, arrayType);
        auto loaded = Builder.Emit1("lde"_op, { destPtr });
        Builder.SetType(loaded, FromAstType(expr->Type, Module.Types));
        co_return TValueWithBlock{ loaded, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
    } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TAssignExpr>(expr)) {
        auto asg = maybeAsg.Cast();
        auto rhs = co_await Lower(asg->Value, scope);
        if (!rhs.Value) co_return TError(asg->Value->Location, "right-hand side of assignment must be a number");
        auto sidOpt = Context.Lookup(asg->Name, scope.Id);
        if (!sidOpt) co_return TError(asg->Location, "assignment to undefined");

        auto node = Context.GetSymbolNode(NSemantics::TSymbolId{sidOpt->Id});
        auto slotType = FromAstType(node->Type, Module.Types);

        auto storeSlot = TSlot{sidOpt->Id};
        auto localSlot = TLocal{sidOpt->FunctionLevelIdx};
        if (localSlot.Idx >= 0) {
            Builder.SetType(localSlot, slotType);
        }
        TOperand storeOperand = (localSlot.Idx >= 0) ? TOperand{localSlot} : TOperand{storeSlot};
        // slot type was set on variable declaration

        // copy-paste
        // TODO: copy-paste
        if (rhs.Value->Type == TOperand::EType::Imm) {
            // Materialize immediate string literals into a tmp
            if (rhs.Value->Imm.TypeId == lowStringTypeId) {
                // TODO: create proper kind for string literal
                auto constructorId = co_await GlobalSymbolId("str_from_lit");
                Builder.Emit0("arg"_op, {*rhs.Value});
                auto materializedString = Builder.Emit1("call"_op, {TImm{constructorId}});
                Builder.SetType(materializedString, slotType);
                *rhs.Value = materializedString;
                rhs.Ownership = EOwnership::Owned;
            }
        }

        // TODO: copy-paste
        // For string destinations: retain borrowed RHS before releasing dest (safe for s := s)
        if (NAst::TMaybeType<NAst::TStringType>(node->Type)) {
            if (rhs.Ownership == EOwnership::Borrowed) {
                // Borrowed RHS: retain before touching destination
                auto retainId = co_await GlobalSymbolId("str_retain");
                Builder.Emit0("arg"_op, {*rhs.Value});
                Builder.Emit0("call"_op, { TImm{retainId} });
            }
        }
        {
            // delete previous string value if any (only for direct slots/locals, not references)
            if (NAst::TMaybeType<NAst::TStringType>(node->Type) && !NAst::TMaybeType<NAst::TReferenceType>(node->Type)) {
                auto dtorId = co_await GlobalSymbolId("str_release");
                TTmp currentVal = Builder.Emit1("load"_op, {storeOperand});
                Builder.SetType(currentVal, slotType);
                Builder.Emit0("arg"_op, { currentVal });
                Builder.Emit0("call"_op, { TImm{dtorId} });
            }
        }

        if (auto maybeRef = NAst::TMaybeType<NAst::TReferenceType>(node->Type)) {
            auto ref = maybeRef.Cast();
            auto addrTmp = Builder.Emit1("load"_op, {storeOperand});
            Builder.SetType(addrTmp, FromAstType(node->Type, Module.Types));

            // Reference assignment semantics for strings: retain RHS and release previous dest
            // see cases/ref/index_ref
            if (NAst::TMaybeType<NAst::TStringType>(ref->ReferencedType)) {
                // Ensure destination gets its own ref. Retain regardless of RHS ownership,
                // so any subsequent release of a temporary won't drop the value stored at dest.
                auto retainId = co_await GlobalSymbolId("str_retain");
                Builder.Emit0("arg"_op, {*rhs.Value});
                Builder.Emit0("call"_op, { TImm{retainId} });

                // Release previously stored value (if any)
                auto prevVal = Builder.Emit1("lde"_op, { addrTmp });
                Builder.SetType(prevVal, FromAstType(ref->ReferencedType, Module.Types));
                auto dtorId = co_await GlobalSymbolId("str_release");
                Builder.Emit0("arg"_op, { prevVal });
                Builder.Emit0("call"_op, { TImm{ dtorId } });
            }

            Builder.Emit0("ste"_op, {addrTmp, *rhs.Value});
        } else {
            Builder.Emit0("stre"_op, {storeOperand, *rhs.Value});
        }

        // store does not produce a value
        co_return TValueWithBlock{ {}, Builder.CurrentBlockLabel() };
    } else if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(expr)) {
        auto ident = maybeIdent.Cast();
        auto tmp = co_await LoadVar(ident->Name, scope, ident->Location);
        // if variable is a ref, need to dereference
        auto var = Context.Lookup(ident->Name, scope.Id);
        auto node = Context.GetSymbolNode(NSemantics::TSymbolId{var->Id});
        // tmp contains the address of the variable
        if (auto maybeRef = NAst::TMaybeType<NAst::TReferenceType>(node->Type)) {
            auto ref = maybeRef.Cast();
            auto derefTmp = Builder.Emit1("lde"_op, { tmp });
            Builder.SetType(derefTmp, FromAstType(ref->ReferencedType, Module.Types));
            tmp = derefTmp;
        }

        // Borrowed for stack values ignored
        // For strings, we need to track destructors for owned temporaries
        co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
    } else if (auto maybeVar = NAst::TMaybeNode<NAst::TVarStmt>(expr)) {
        // TODO: zero memory for strings?
        auto var = maybeVar.Cast();
        auto name = var->Name;
        auto sidOpt = Context.Lookup(var->Name, scope.Id);
        if (!sidOpt) {
            co_return TError(var->Location, "variable has no binding");
        }
        if (sidOpt->FunctionLevelIdx >= 0) {
            Builder.SetType(TLocal{sidOpt->FunctionLevelIdx}, FromAstType(var->Type, Module.Types));
        }
        if (NAst::TMaybeType<NAst::TStringType>(var->Type)
            && sidOpt->FunctionLevelIdx >= 0 && name != "$$return" /*owned by caller*/)
        {
            auto dtorId = co_await GlobalSymbolId("str_release");
            TOperand arg = (sidOpt->FunctionLevelIdx >= 0)
                ? TOperand { TLocal{ sidOpt->FunctionLevelIdx } }
                : TOperand { TSlot{ sidOpt->Id } };
            std::vector<TOperand> args = {
                arg
            };
            auto node = Context.GetSymbolNode(NSemantics::TSymbolId{ sidOpt->Id });
            std::vector<int> types = {
                FromAstType(node->Type, Module.Types)
            };
            TDestructor dtor = TDestructor {
                .Args = std::move(args),
                .TypeIds = std::move(types),
                .FunctionId = dtorId
            };
            PendingDestructors.emplace_back(std::move(dtor));
        }
        if (auto maybeArrayType = NAst::TMaybeType<NAst::TArrayType>(var->Type)) {
            auto arrayType = FromAstType(var->Type, Module.Types);
            auto ctorId = co_await GlobalSymbolId("array_create");

            auto totalElements = Context.Lookup("$$" + var->Name + "_mulacc0", scope.Id);
            if (!totalElements) co_return TError(var->Location, std::string("undefined name"));
            TOperand op = (totalElements->FunctionLevelIdx >= 0)
                ? TOperand { TLocal{ totalElements->FunctionLevelIdx } }
                : TOperand { TSlot{ totalElements->Id } };
            auto tmp = Builder.Emit1("load"_op, {op});
            auto i64 = Module.Types.I(EKind::I64);
            Builder.SetType(tmp, i64);
            auto arraySize = Builder.Emit1("*"_op, {tmp, TImm{8, i64}}); // TODO: constant depends on element type
            Builder.SetType(arraySize, i64);

            Builder.Emit0("arg"_op, {arraySize});
            auto arrayPtr = Builder.Emit1("call"_op, {TImm{ctorId}});
            Builder.SetType(arrayPtr, arrayType);

            auto dtorId = co_await GlobalSymbolId("array_destroy");
            bool arrayOfStrings = false;
            if (Module.Types.UnderlyingType(arrayType) == lowStringTypeId) {
                dtorId = co_await GlobalSymbolId("array_str_destroy");
                arrayOfStrings = true;
            }
            TOperand arg = (sidOpt->FunctionLevelIdx >= 0)
                ? TOperand { TLocal{ sidOpt->FunctionLevelIdx } }
                : TOperand { TSlot{ sidOpt->Id } };
            std::vector<TOperand> args = {
                arg
            };
            if (arrayOfStrings) {
                args.push_back(arraySize);
            }
            Builder.Emit0("stre"_op, {arg, arrayPtr});
            auto node = Context.GetSymbolNode(NSemantics::TSymbolId{ sidOpt->Id });
            std::vector<int> types = { arrayType };
            TDestructor dtor = TDestructor {
                .Args = std::move(args),
                .TypeIds = std::move(types),
                .FunctionId = dtorId
            };
            PendingDestructors.emplace_back(std::move(dtor));
        }
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(expr)) {
        auto fun = maybeFun.Cast();
        auto name = fun->Name;
        if (scope.Id.Id != 0) {
            co_return TError(fun->Location, "nested function declarations not supported");
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
        std::vector<TLocal> args; args.reserve(params.size());
        int i = 0;
        for (auto& p : params) {
            auto psid = Context.Lookup(p->Name, NSemantics::TScopeId{funScope});
            if (!psid) {
                co_return TError(p->Location, "parameter has no binding");
            }
            auto local = TLocal{ psid->FunctionLevelIdx };
            args.push_back(local);
            i++;
        }

        // auto currentFuncIdx = Builder.CurrentFunctionIdx(); // needed for nested functions
        auto funcIdx = Builder.NewFunction(name, args, sidOpt->Id);
        auto returnType = FromAstType(fun->RetType, Module.Types);
        Builder.SetReturnType(returnType);
        for (auto& a : args) {
            Builder.SetType(a, FromAstType(type->ParamTypes[&a - &args[0]], Module.Types));
        }
        if (NAst::TMaybeType<NAst::TStringType>(fun->RetType)) {
            // TODO: remove me
            // clutch: support string returnType
            Module.Functions[Builder.CurrentFunctionIdx()].ReturnTypeIsString = true;
        }

        // Create a dedicated final return block label beforehand and pass it as BreakLabel for early exits.
        auto endLabel = Builder.NewLabel();
        auto loweredBody = co_await Lower(body, TBlockScope {
            .FuncIdx = funcIdx,
            .Id = NSemantics::TScopeId{funScope},
            .BreakLabel = endLabel,
            .ContinueLabel = std::nullopt
        });
        // Jump to final return block unless already terminated by earlier logic.
        if (!Builder.IsCurrentBlockTerminated()) {
            Builder.Emit0("jmp"_op, { endLabel });
        }
        // Materialize final return block and emit single ret there.
        Builder.NewBlock(endLabel);
        if (!NAst::TMaybeType<NAst::TVoidType>(fun->RetType)) {
            // return value = lowered IdentExpr named '$$return' in function scope
            auto returnVar = co_await LoadVar("$$return", TBlockScope {
                .FuncIdx = funcIdx,
                .Id = NSemantics::TScopeId{funScope},
                .BreakLabel = std::nullopt,
                .ContinueLabel = std::nullopt
            }, fun->Location);
            Builder.Emit0("ret"_op, {returnVar});
        } else {
            Builder.Emit0("ret"_op, {});
        }
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
            if (!sidOpt) co_return TError(ident->Location, std::string("undefined function: `") + ident->Name + "' in scope: " + std::to_string(scope.Id.Id));
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

        std::vector<std::pair<TOperand, EOwnership>> argv;
        argv.reserve(call->Args.size());
        int i = 0;
        for (auto& a : call->Args) {
            auto& argType = (*argTypes)[i++];
            TValueWithBlock av;

            if (NAst::TMaybeType<NAst::TReferenceType>(argType)) {
                // a must be an identifier
                auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(a);
                if (!maybeIdent) {
                    co_return TError(a->Location, "argument for reference parameter must be an identifier");
                }
                av.Value = co_await LoadVar(maybeIdent.Cast()->Name, scope, a->Location, true /*address*/);
                Builder.SetType(av.Value->Tmp, FromAstType(argType, Module.Types));
            } else {
                av = co_await Lower(a, scope);
            }

            if (!av.Value) co_return TError(a->Location, "invalid argument");

            if (av.Value->Type == TOperand::EType::Imm && av.Value->Imm.TypeId == lowStringTypeId && (!funDecl->IsExternal() || funDecl->RequireArgsMaterialization)) {
                // Argument is a string literal pointer: materialize to string object
                if (NAst::TMaybeType<NAst::TStringType>(argType)) {
                    auto constructorId = co_await GlobalSymbolId("str_from_lit");
                    Builder.Emit0("arg", {*av.Value});
                    auto materializedString = Builder.Emit1("call"_op, {TImm{constructorId}});
                    Builder.SetType(materializedString, FromAstType(argType, Module.Types));
                    av.Value = materializedString;
                    av.Ownership = EOwnership::Owned;
                }
            }
            argv.emplace_back(*av.Value, av.Ownership);
        }
        for (auto [arg, _] : argv) {
            Builder.Emit0("arg"_op, {arg});
        }
        // Decide if callee returns a value (non-void). If void -> Emit0
        bool returnsValue = false;
        if (NAst::TMaybeType<NAst::TVoidType>(returnType)) {
            returnsValue = false;
        } else {
            returnsValue = true;
        }

        std::optional<TOperand> tmp = std::nullopt;
        if (returnsValue) {
            tmp = Builder.Emit1("call"_op, {TImm{calleeSymId}});
            Builder.SetType(tmp->Tmp, FromAstType(returnType, Module.Types));
        } else {
            Builder.Emit0("call"_op, {TImm{calleeSymId}});
        }
        for (auto [arg, ownership] : argv) {
            // For string arguments passed as owned temporaries: release after call
            if (ownership == EOwnership::Owned) {
                // TODO: check that arg is string type
                // TODO: destructors for other types
                auto dtorId = co_await GlobalSymbolId("str_release");
                Builder.Emit0("arg"_op, {arg});
                Builder.Emit0("call"_op, { TImm{dtorId} });
            }
        }
        co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel(),
            NAst::TMaybeType<NAst::TStringType>(returnType) ? EOwnership::Owned : EOwnership::Unkwnown };
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

    std::vector<int> argTypes; argTypes.reserve(funcDecl.Params.size());
    int returnType = FromAstType(funcDecl.RetType, Module.Types);
    for (auto& p : funcDecl.Params) {
        argTypes.push_back(FromAstType(p->Type, Module.Types));
    }

    TExternalFunction func {
        .Name = funcDecl.Name,
        .MangledName = funcDecl.MangledName,
        .ArgTypes = std::move(argTypes),
        .ReturnTypeId = returnType,
        .Addr = funcDecl.Ptr,
        .Packed = funcDecl.Packed,
        .SymId = symbolId
    };
    int funIdx = Module.ExternalFunctions.size();
    Module.ExternalFunctions.push_back(func);
    Module.SymIdToExtFuncIdx[symbolId] = funIdx;
}

TExpectedTask<int, TError, TLocation> TAstLowerer::GlobalSymbolId(const std::string& name) {
    auto sidOpt = Context.Lookup(name, NSemantics::TScopeId{0});
    if (sidOpt) {
        co_return sidOpt->Id;
    }
    co_return TError({}, "undefined global symbol: " + name);
}

void TAstLowerer::ImportExternalFunctions() {
    for (const auto& [symbolId, func] : Context.GetExternalFunctions()) {
        ImportExternalFunction(symbolId, *func);
    }
}

std::expected<std::monostate, TError> TAstLowerer::LowerTop(const NAst::TExprPtr& expr) {
    ImportExternalFunctions();
    auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(expr);
    if (!maybeBlock) {
        return std::unexpected(TError(expr->Location, "Root expr must be a block"));
    }
    auto block = maybeBlock.Cast();

    auto scope = TBlockScope {
        .FuncIdx = -1,
        .Id = NSemantics::TScopeId{0}
    };
    block->Scope = scope.Id.Id;

    bool functionSeen = false;
    int constructorFunctionId = -1;
    int destructorFunctionId = -1;
    std::string constructorFunctionName = "$$module_constructor";
    std::string destructorFunctionName = "$$module_destructor";

    auto switchToConstructorFunction = [&]() {
        if (constructorFunctionId == -1) {
            constructorFunctionId = Builder.NewFunction(constructorFunctionName, {}, -1 /*symbolId*/);
            Builder.SetReturnType(Module.Types.I(EKind::Void));
        } else {
            Builder.SetCurrentFunction(constructorFunctionId);
        }
    };

    std::function<std::expected<std::monostate, TError>(const std::shared_ptr<NAst::TBlockExpr>&, const TBlockScope&)> lowerTopBlock = [&](const std::shared_ptr<NAst::TBlockExpr>& block, const TBlockScope& scope) -> std::expected<std::monostate, TError>
    {
        for (auto& s : block->Stmts) {
            if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(s)) {
                auto res = Lower(s, scope).result();
                if (!res) {
                    return std::unexpected(res.error());
                }
                functionSeen = true;
            } else if (auto maybeBlock = NAst::TMaybeNode<NAst::TBlockExpr>(s)) {
                auto res = lowerTopBlock(maybeBlock.Cast(), scope);
                if (!res) {
                    return std::unexpected(res.error());
                }
            } else if (auto maybeVar = NAst::TMaybeNode<NAst::TVarStmt>(s)) {
                if (functionSeen) {
                    return std::unexpected(TError(s->Location, "variable declarations must appear before function declarations"));
                }
                auto var = maybeVar.Cast();
                auto sidOpt = Context.Lookup(var->Name, scope.Id);
                if (!sidOpt) {
                    return std::unexpected(TError(var->Location, "var declaration has no binding"));
                }
                auto slotType = FromAstType(var->Type, Module.Types);
                if (Module.GlobalTypes.size() <= (size_t)sidOpt->Id) {
                    Module.GlobalTypes.resize(sidOpt->Id + 1);
                    Module.GlobalValues.resize(sidOpt->Id + 1); // TODO: unused
                }
                Module.GlobalTypes[sidOpt->Id] = slotType;

                if (NAst::TMaybeType<NAst::TArrayType>(var->Type)) {
                    // Arrays need to be constructed in the module constructor
                    switchToConstructorFunction();
                    auto lowered = Lower(s, scope).result();
                    if (!lowered) {
                        return std::unexpected(lowered.error());
                    }
                } else if (NAst::TMaybeType<NAst::TStringType>(var->Type)) {
                    // Strings need to be constructed in the module constructor
                    switchToConstructorFunction();
                    auto lowered = Lower(s, scope).result();
                    if (!lowered) {
                        return std::unexpected(lowered.error());
                    }
                }

            } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TAssignExpr>(s)) {
                if (functionSeen) {
                    return std::unexpected(TError(s->Location, "variable assignments must appear before function declarations"));
                }
                auto asg = maybeAsg.Cast();
                auto maybeNumber = NAst::TMaybeNode<NAst::TNumberExpr>(asg->Value);
                auto sid = Context.Lookup(asg->Name, scope.Id);
                if (!sid) {
                    return std::unexpected(TError(s->Location, "undefined variable: " + asg->Name));
                }
                if (maybeNumber) {
                    auto num = maybeNumber.Cast();
                    if (num->IsFloat) {
                        Module.GlobalValues[sid->Id] = TImm{.Value = std::bit_cast<int64_t>(num->FloatValue), .TypeId = Module.Types.I(EKind::F64)};
                    } else {
                        Module.GlobalValues[sid->Id] = TImm{.Value = num->IntValue, .TypeId = Module.Types.I(EKind::I64)};
                    }
                }
                auto maybeString = NAst::TMaybeNode<NAst::TStringLiteralExpr>(asg->Value);
                if (maybeString) {
                    auto str = maybeString.Cast();
                    auto id = Builder.StringLiteral(str->Value);
                    Module.GlobalValues[sid->Id] = TImm{.Value = id, .TypeId = Module.Types.Ptr(Module.Types.I(EKind::I8))};
                    // string globals are pointers
                    Module.GlobalTypes[sid->Id] = Module.Types.Ptr(Module.Types.I(EKind::I8));
                }

                switchToConstructorFunction();
                auto lowered = Lower(s, scope).result();
                if (!lowered) {
                    return std::unexpected(lowered.error());
                }
            } else {
                return std::unexpected(TError(s->Location, "Unexpected top-level statement: " + s->ToString()));
            }
        }

        return {};
    };

    lowerTopBlock(block, scope);
    if (constructorFunctionId != -1) {
        Builder.SetCurrentFunction(constructorFunctionId);
        Builder.Emit0("ret"_op, {});
        Module.ModuleConstructorFunctionId = constructorFunctionId;
    }

    if (!PendingDestructors.empty()) {
        destructorFunctionId = Builder.NewFunction(destructorFunctionName, {}, -2 /*symbolId*/);
        Builder.SetReturnType(Module.Types.I(EKind::Void));
        for (auto& dtor : PendingDestructors) {
            for (auto& arg : dtor.Args) {
                if (arg.Type == TOperand::EType::Slot) {
                    // load slot value
                    auto tmp = Builder.Emit1("load"_op, {arg});
                    Builder.SetType(tmp, Module.GlobalTypes[arg.Slot.Idx]);
                    arg = tmp;
                }
                Builder.Emit0("arg"_op, {arg});
            }
            Builder.Emit0("call"_op, { TImm{ dtor.FunctionId } });
        }
        Builder.Emit0("ret"_op, {});
        Module.ModuleDestructorFunctionId = destructorFunctionId;
        PendingDestructors.clear();
    }

    return {};
}


} // namespace NIR
} // namespace NQumir
