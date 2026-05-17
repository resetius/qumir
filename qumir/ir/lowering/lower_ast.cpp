#include "lower_ast.h"
#include "qumir/ir/builder.h"
#include "qumir/parser/parser.h"
#include "qumir/parser/core/printer.h"
#include "qumir/parser/type.h"
#include "qumir/error.h"

#include <iostream>
#include <sstream>
#include <cassert>
#include <functional>
#include <algorithm>

namespace NQumir {
namespace NIR {

using namespace NLiterals;

namespace {

NAst::TTypePtr PhysicalCallResultType(NAst::TTypePtr type)
{
    if (auto futureResult = NAst::FutureResultType(type)) {
        return futureResult;
    }
    return type;
}

} // namespace

TOperand TAstLowerer::AllocLayoutStorage(NSemantics::TSymbolInfo symbol, int typeId)
{
    if (symbol.FunctionLevelIdx >= 0) {
        return TOperand{Builder.AllocLocal(typeId)};
    }

    if (NextHiddenGlobalSlot < 0) {
        NextHiddenGlobalSlot = static_cast<int32_t>(Context.GetSymbols().size());
    }

    TSlot slot{NextHiddenGlobalSlot++};
    if (Module.GlobalTypes.size() <= static_cast<size_t>(slot.Idx)) {
        Module.GlobalTypes.resize(slot.Idx + 1);
        Module.GlobalValues.resize(slot.Idx + 1);
    }
    Module.GlobalTypes[slot.Idx] = typeId;
    return TOperand{slot};
}

TTmp TAstLowerer::LoadLayoutOperand(TOperand operand)
{
    auto tmp = Builder.Emit1("load"_op, {operand});
    Builder.SetType(tmp, Module.Types.I(EKind::I64));
    return tmp;
}

TExpectedTask<TAstLowerer::TArrayLayout, TError, TLocation> TAstLowerer::LowerArrayLayout(
    NSemantics::TSymbolInfo symbol,
    const std::vector<std::pair<NAst::TExprPtr, NAst::TExprPtr>>& bounds,
    TBlockScope scope,
    const TLocation& loc)
{
    if (bounds.empty()) {
        co_return TError(loc, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
    }

    auto i64 = Module.Types.I(EKind::I64);
    TArrayLayout layout;
    layout.LBounds.resize(bounds.size());
    layout.DimSizes.resize(bounds.size());
    layout.Strides.resize(bounds.size());

    TOperand prevStride = TImm{1, i64};
    for (int i = static_cast<int>(bounds.size()) - 1; i >= 0; --i) {
        const auto& [lboundExpr, rboundExpr] = bounds[i];
        auto lboundValue = co_await Lower(lboundExpr, scope);
        if (!lboundValue.Value) {
            co_return TError(lboundExpr->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
        }
        auto rboundValue = co_await Lower(rboundExpr, scope);
        if (!rboundValue.Value) {
            co_return TError(rboundExpr->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
        }

        auto lboundStorage = AllocLayoutStorage(symbol, i64);
        auto dimSizeStorage = AllocLayoutStorage(symbol, i64);
        auto strideStorage = AllocLayoutStorage(symbol, i64);

        Builder.Emit0("stre"_op, {lboundStorage, *lboundValue.Value});

        auto dimDiff = Builder.Emit1("-"_op, {*rboundValue.Value, *lboundValue.Value});
        Builder.SetType(dimDiff, i64);
        auto dimSize = Builder.Emit1("+"_op, {dimDiff, TImm{1, i64}});
        Builder.SetType(dimSize, i64);
        Builder.Emit0("stre"_op, {dimSizeStorage, dimSize});

        auto stride = Builder.Emit1("*"_op, {prevStride, dimSize});
        Builder.SetType(stride, i64);
        Builder.Emit0("stre"_op, {strideStorage, stride});

        layout.LBounds[i] = lboundStorage;
        layout.DimSizes[i] = dimSizeStorage;
        layout.Strides[i] = strideStorage;
        prevStride = stride;
    }

    layout.TotalElements = layout.Strides[0];
    ArrayLayouts[symbol.Id] = layout;
    co_return layout;
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerWhile(std::shared_ptr<NAst::TWhileStmtExpr> loop, TBlockScope scope)
{
    auto entryId = Builder.CurrentBlockIdx();
    auto [condLabel, condId] = Builder.NewBlock();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {condLabel});

    Builder.SetCurrentBlock(condId);
    auto cond = co_await Lower(loop->Cond, scope);
    if (!cond.Value) co_return TError(loop->Cond->Location, TErrorString::Get<EErrorId::WHILE_CONDITION_NOT_NUMBER>());
    Builder.Emit0("cmp"_op, {*cond.Value, bodyLabel, endLabel});

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

    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerRepeat(std::shared_ptr<NAst::TRepeatStmtExpr> loop, TBlockScope scope)
{
    auto entryId = Builder.CurrentBlockIdx();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto [condLabel, condId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {bodyLabel});

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

    Builder.SetCurrentBlock(condId);
    auto cond = co_await Lower(loop->Cond, scope);
    if (!cond.Value) co_return TError(loop->Cond->Location, TErrorString::Get<EErrorId::REPEAT_CONDITION_NOT_NUMBER>());
    Builder.Emit0("cmp"_op, {*cond.Value, bodyLabel, endLabel});

    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerFor(std::shared_ptr<NAst::TForStmtExpr> loop, TBlockScope scope)
{
    const auto i64 = Module.Types.I(EKind::I64);

    auto sidOpt = Context.Lookup(loop->VarName, scope.Id);
    if (!sidOpt) {
        co_return TError(loop->Location, TErrorString::Get<EErrorId::ASSIGNMENT_TO_UNDEFINED>());
    }
    TOperand varOperand = sidOpt->FunctionLevelIdx >= 0
        ? TOperand{TLocal{sidOpt->FunctionLevelIdx}}
        : TOperand{TSlot{sidOpt->Id}};
    if (sidOpt->FunctionLevelIdx >= 0) {
        Builder.SetType(TLocal{sidOpt->FunctionLevelIdx}, i64);
    }

    auto toLocal = Builder.AllocLocal(i64);
    auto stepLocal = Builder.AllocLocal(i64);
    auto nextLocal = Builder.AllocLocal(i64);

    auto storeLocal = [&](TLocal local, TOperand value) {
        Builder.Emit0("stre"_op, {TOperand{local}, value});
    };
    auto loadLocal = [&](TLocal local) {
        auto tmp = Builder.Emit1("load"_op, {TOperand{local}});
        Builder.SetType(tmp, i64);
        return tmp;
    };

    auto from = co_await Lower(loop->From, scope);
    if (!from.Value) co_return TError(loop->From->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
    Builder.Emit0("stre"_op, {varOperand, *from.Value});

    if (loop->Step) {
        auto step = co_await Lower(loop->Step, scope);
        if (!step.Value) co_return TError(loop->Step->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
        storeLocal(stepLocal, *step.Value);
    } else {
        storeLocal(stepLocal, TImm{1, i64});
    }

    auto initialNext = Builder.Emit1("load"_op, {varOperand});
    Builder.SetType(initialNext, i64);
    storeLocal(nextLocal, initialNext);

    auto to = co_await Lower(loop->To, scope);
    if (!to.Value) co_return TError(loop->To->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
    storeLocal(toLocal, *to.Value);

    auto entryId = Builder.CurrentBlockIdx();
    auto [condLabel, condId] = Builder.NewBlock();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto [postLabel, postId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {condLabel});

    Builder.SetCurrentBlock(condId);
    auto toValue = loadLocal(toLocal);
    auto nextValue = loadLocal(nextLocal);
    auto stepValue = loadLocal(stepLocal);
    auto diff = Builder.Emit1("-"_op, {toValue, nextValue});
    Builder.SetType(diff, i64);
    auto product = Builder.Emit1("*"_op, {diff, stepValue});
    Builder.SetType(product, i64);
    auto condValue = Builder.Emit1(">="_op, {product, TImm{0, i64}});
    Builder.SetType(condValue, Module.Types.I(EKind::I1));
    Builder.Emit0("cmp"_op, {condValue, bodyLabel, endLabel});

    Builder.SetCurrentBlock(bodyId);
    auto current = loadLocal(nextLocal);
    Builder.Emit0("stre"_op, {varOperand, current});
    co_await Lower(loop->Body, TBlockScope{
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = postLabel
    });
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {postLabel});
    }

    Builder.SetCurrentBlock(postId);
    auto postNext = loadLocal(nextLocal);
    auto postStep = loadLocal(stepLocal);
    auto updated = Builder.Emit1("+"_op, {postNext, postStep});
    Builder.SetType(updated, i64);
    storeLocal(nextLocal, updated);
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {condLabel});
    }

    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerTimes(std::shared_ptr<NAst::TTimesStmtExpr> loop, TBlockScope scope)
{
    const auto i64 = Module.Types.I(EKind::I64);

    auto toLocal = Builder.AllocLocal(i64);
    auto nextLocal = Builder.AllocLocal(i64);

    auto count = co_await Lower(loop->Count, scope);
    if (!count.Value) co_return TError(loop->Count->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
    Builder.Emit0("stre"_op, {TOperand{nextLocal}, TImm{1, i64}});
    auto toValue = Builder.Emit1("+"_op, {*count.Value, TImm{1, i64}});
    Builder.SetType(toValue, i64);
    Builder.Emit0("stre"_op, {TOperand{toLocal}, toValue});

    auto loadLocal = [&](TLocal local) {
        auto tmp = Builder.Emit1("load"_op, {TOperand{local}});
        Builder.SetType(tmp, i64);
        return tmp;
    };

    auto entryId = Builder.CurrentBlockIdx();
    auto [condLabel, condId] = Builder.NewBlock();
    auto [bodyLabel, bodyId] = Builder.NewBlock();
    auto [postLabel, postId] = Builder.NewBlock();
    auto endLabel = Builder.NewLabel();

    Builder.SetCurrentBlock(entryId);
    Builder.Emit0("jmp"_op, {condLabel});

    Builder.SetCurrentBlock(condId);
    auto nextValue = loadLocal(nextLocal);
    auto limitValue = loadLocal(toLocal);
    auto condValue = Builder.Emit1("!="_op, {nextValue, limitValue});
    Builder.SetType(condValue, Module.Types.I(EKind::I1));
    Builder.Emit0("cmp"_op, {condValue, bodyLabel, endLabel});

    Builder.SetCurrentBlock(bodyId);
    co_await Lower(loop->Body, TBlockScope{
        .FuncIdx = scope.FuncIdx,
        .Id = scope.Id,
        .BreakLabel = endLabel,
        .ContinueLabel = postLabel
    });
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {postLabel});
    }

    Builder.SetCurrentBlock(postId);
    auto postNext = loadLocal(nextLocal);
    auto updated = Builder.Emit1("+"_op, {postNext, TImm{1, i64}});
    Builder.SetType(updated, i64);
    Builder.Emit0("stre"_op, {TOperand{nextLocal}, updated});
    if (!Builder.IsCurrentBlockTerminated()) {
        Builder.Emit0("jmp"_op, {condLabel});
    }

    Builder.NewBlock(endLabel);
    co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
}

TExpectedTask<TTmp, TError, TLocation> TAstLowerer::LoadVar(const std::string& name, TBlockScope scope, const TLocation& loc, bool takeRefOfNotRef)
{
    auto var = Context.Lookup(name, scope.Id);
    if (!var) {
        co_return TError(loc, TErrorString::Get<EErrorId::UNDEFINED_VARIABLE>(name));
    }

    auto node = Context.GetSymbolNode(NSemantics::TSymbolId{var->Id});
    if (!node) {
        co_return TError(loc, TErrorString::Get<EErrorId::UNDEFINED_VARIABLE>(name));
    }
    if (NAst::TMaybeType<NAst::TReferenceType>(node->Type) && takeRefOfNotRef) {
        takeRefOfNotRef = false;
    }

    TOperand op = (var->FunctionLevelIdx >= 0)
        ? TOperand{ TLocal{ var->FunctionLevelIdx } }
        : TOperand{ TSlot{ var->Id } };

    auto nodeType = NAst::UnwrapNamedType(node->Type);
    // "load" returns a value; "lea" returns an address for ref arguments.
    TOp opcode = takeRefOfNotRef ? "lea"_op : "load"_op;
    auto tmp = Builder.Emit1(opcode, { op });
    Builder.SetType(tmp, FromAstType(node->Type, Module.Types));
    co_return tmp;
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::LowerIndices(NSemantics::TSymbolInfo symbol, const std::vector<NAst::TExprPtr>& indices, TBlockScope scope, int elemSize)
{
    int n = indices.size() - 1;
    int i = n;
    auto i64 = Module.Types.I(EKind::I64);

    auto layoutIt = ArrayLayouts.find(symbol.Id);
    if (layoutIt == ArrayLayouts.end()) {
        co_return TError(indices.empty() ? TLocation{} : indices.front()->Location, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
    }
    const auto& layout = layoutIt->second;

    std::optional<TTmp> prev;

    for (; i >= 0; --i) {
        auto indexRes = co_await Lower(indices[i], scope);
        if (!indexRes.Value) {
            co_return TError(indices[i]->Location, TErrorString::Get<EErrorId::ARRAY_INDEX_NOT_NUMBER>());
        }
        // totalIndex = sum (index - lowerBound) * stride_i
        // stride_n = 1
        // stride_{n-1} = mulAccName_{n}

        auto tmp = LoadLayoutOperand(layout.LBounds[i]);
        tmp = Builder.Emit1("-"_op, {*indexRes.Value, tmp});
        Builder.SetType(tmp, i64);
        if (i != n) {
            auto stride = LoadLayoutOperand(layout.Strides[i + 1]);
            tmp = Builder.Emit1("*"_op, {tmp, stride});
            Builder.SetType(tmp, i64);
        }

        if (prev) {
            tmp = Builder.Emit1("+"_op, {tmp, *prev});
            Builder.SetType(tmp, i64);
        }

        prev = tmp;
    }
    *prev = Builder.Emit1("*"_op, {*prev, TImm{elemSize, i64}}); // byte offset
    Builder.SetType(*prev, i64);
    co_return TValueWithBlock{ *prev, Builder.CurrentBlockLabel() };
}

TExpectedTask<TAstLowerer::TValueWithBlock, TError, TLocation> TAstLowerer::Lower(const NAst::TExprPtr& inputExpr, TBlockScope scope) {
    int lowStringTypeId = Module.Types.Ptr(Module.Types.I(EKind::I8));
    NAst::TExprPtr expr = inputExpr;
    bool isAwait = false;
    if (auto maybeAwait = NAst::TMaybeNode<NAst::TAwaitExpr>(expr)) {
        auto awaitExpr = maybeAwait.Cast();
        if (!NAst::TMaybeNode<NAst::TCallExpr>(awaitExpr->Operand)) {
            co_return TError(awaitExpr->Location, "await lowering expects a call operand");
        }
        expr = awaitExpr->Operand;
        isAwait = true;
    }

    if (auto maybeCast = NAst::TMaybeNode<NAst::TCastExpr>(expr)) {
        auto cast = maybeCast.Cast();
        auto operand = co_await Lower(cast->Operand, scope);
        if (!operand.Value) co_return TError(cast->Operand->Location, TErrorString::Get<EErrorId::OPERAND_OF_CAST_NOT_VALUE>());
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
        } else if (FromAstType(NAst::UnwrapNamedType(expr->Type), Module.Types)
            == FromAstType(NAst::UnwrapNamedType(cast->Operand->Type), Module.Types)) {
            tmp = Builder.Emit1("mov"_op, {*operand.Value});
        } else {
            co_return TError(cast->Location, TErrorString::Get<EErrorId::UNSUPPORTED_CAST_TYPES>(std::string(cast->Operand->Type->TypeName()), std::string(expr->Type->TypeName())));
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
    } else if (auto maybeSeq = NAst::TMaybeNode<NAst::TSeqExpr>(expr)) {
        std::optional<TOperand> last;
        auto seq = maybeSeq.Cast();
        for (auto& s : seq->Stmts) {
            auto r = co_await Lower(s, scope);
            last = r.Value;

            if (r.Ownership == EOwnership::Owned) {
                last = std::nullopt;
                auto dtorId = co_await GlobalSymbolId("str_release");
                Builder.Emit0("arg"_op, {*r.Value});
                Builder.Emit0("call"_op, { TImm{ dtorId } });
                last = r.Value;
            }

            if (Builder.IsCurrentBlockTerminated()) {
                break;
            }
        }
        co_return TValueWithBlock{ last, Builder.CurrentBlockLabel() };
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
        if (!operand.Value) co_return TError(unary->Operand->Location, TErrorString::Get<EErrorId::OPERAND_OF_UNARY_NOT_NUMBER>());
        if (unary->Operator == '-') {
            auto tmp = Builder.Emit1("neg"_op, {*operand.Value});
            Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
            co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
        } else if (unary->Operator == '!'_op) {
            auto tmp = Builder.Emit1("!"_op, {*operand.Value});
            Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
            co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
        } else if (unary->Operator == '~'_op) {
            auto tmp = Builder.Emit1("~"_op, {*operand.Value});
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
        if (!leftNum) co_return TError(binary->Location, TErrorString::Get<EErrorId::BINARY_OPERANDS_NOT_NUMBERS>());
        if (!isLazy && !rightNum) co_return TError(binary->Location, TErrorString::Get<EErrorId::BINARY_OPERANDS_NOT_NUMBERS>());

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
                if (!r.Value) co_return TError(binary->Right->Location, TErrorString::Get<EErrorId::BINARY_OPERANDS_NOT_NUMBERS>());
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
                if (!r.Value) co_return TError(binary->Right->Location, TErrorString::Get<EErrorId::BINARY_OPERANDS_NOT_NUMBERS>());
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
                    auto tmp = Builder.Emit1((uint64_t)binary->Operator.Value /* ast op to ir op mapping */, {*leftNum, *rightNum});
                    Builder.SetType(tmp, FromAstType(expr->Type, Module.Types));
                    co_return TValueWithBlock{ tmp, Builder.CurrentBlockLabel() };
                }
        }
    } else if (auto maybeIfExpr = NAst::TMaybeNode<NAst::TIfExpr>(expr)) {
        auto ife = maybeIfExpr.Cast();
        auto cond = co_await Lower(ife->Cond, scope);
        if (!cond.Value) co_return TError(ife->Cond->Location, TErrorString::Get<EErrorId::IF_CONDITION_NOT_NUMBER>());

        auto entryId = Builder.CurrentBlockIdx();
        auto [thenLabel, thenId] = Builder.NewBlock();
        auto [elseLabel, elseId] = Builder.NewBlock();
        auto endLabel = Builder.NewLabel();

        Builder.SetCurrentBlock(entryId);
        Builder.Emit0("cmp"_op, {*cond.Value, thenLabel, elseLabel});

        Builder.SetCurrentBlock(thenId);
        auto thenRes = co_await Lower(ife->Then, scope);
        if (!thenRes.Value) {
            co_return TError(ife->Then->Location, "Ветвь then в if-expression не возвращает значение");
        }
        Builder.SetCurrentBlock(thenRes.ProducingLabel);
        if (Builder.IsCurrentBlockTerminated()) {
            co_return TError(ife->Then->Location, "Ветвь then в if-expression не должна завершать управление до merge-блока");
        }
        Builder.Emit0("jmp"_op, {endLabel});
        auto thenEdgeLabel = Builder.CurrentBlockLabel();

        Builder.SetCurrentBlock(elseId);
        auto elseRes = co_await Lower(ife->Else, scope);
        if (!elseRes.Value) {
            co_return TError(ife->Else->Location, "Ветвь else в if-expression не возвращает значение");
        }
        Builder.SetCurrentBlock(elseRes.ProducingLabel);
        if (Builder.IsCurrentBlockTerminated()) {
            co_return TError(ife->Else->Location, "Ветвь else в if-expression не должна завершать управление до merge-блока");
        }
        Builder.Emit0("jmp"_op, {endLabel});
        auto elseEdgeLabel = Builder.CurrentBlockLabel();

        Builder.NewBlock(endLabel);
        auto res = Builder.Emit1("phi"_op, {*thenRes.Value, thenEdgeLabel, *elseRes.Value, elseEdgeLabel});
        Builder.SetType(res, FromAstType(expr->Type, Module.Types));
        if (thenRes.Value->Type == TOperand::EType::Tmp) {
            Builder.UnifyTypes(res, thenRes.Value->Tmp);
        }
        if (elseRes.Value->Type == TOperand::EType::Tmp) {
            Builder.UnifyTypes(res, elseRes.Value->Tmp);
        }
        co_return TValueWithBlock{ res, Builder.CurrentBlockLabel() };

    } else if (auto maybeLetExpr = NAst::TMaybeNode<NAst::TLetExpr>(expr)) {
        auto letExpr = maybeLetExpr.Cast();
        if (letExpr->Scope < 0) {
            co_return TError(letExpr->Location, "LetExpr has no resolved scope.");
        }

        TBlockScope letScope {
            .FuncIdx = scope.FuncIdx,
            .Id = NSemantics::TScopeId{letExpr->Scope},
            .BreakLabel = scope.BreakLabel,
            .ContinueLabel = scope.ContinueLabel
        };

        for (const auto& binding : letExpr->Bindings) {
            auto maybeVar = NAst::TMaybeNode<NAst::TVarStmt>(binding.Symbol);
            if (!maybeVar) {
                co_return TError(letExpr->Location, "LetExpr binding '" + binding.Name + "' has no variable symbol.");
            }

            co_await Lower(maybeVar.Cast(), letScope);

            auto assign = std::make_shared<NAst::TAssignExpr>(
                binding.Value ? binding.Value->Location : letExpr->Location,
                binding.Name,
                binding.Value);
            auto assignRes = co_await Lower(assign, letScope);
            Builder.SetCurrentBlock(assignRes.ProducingLabel);
        }

        auto bodyRes = co_await Lower(letExpr->Body, letScope);
        if (!bodyRes.Value) {
            co_return TError(letExpr->Location, "LetExpr body does not return a value.");
        }
        co_return bodyRes;

    } else if (auto maybeIfe = NAst::TMaybeNode<NAst::TIfStmt>(expr)) {
        // If is a statement in this language: no result value, no phi merge.
        auto ife = maybeIfe.Cast();
        auto cond = co_await Lower(ife->Cond, scope);
        if (!cond.Value) co_return TError(ife->Cond->Location, TErrorString::Get<EErrorId::IF_CONDITION_NOT_NUMBER>());

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

    } else if (auto maybeLoop = NAst::TMaybeNode<NAst::TWhileStmtExpr>(expr)) {
        co_return co_await LowerWhile(maybeLoop.Cast(), scope);
    } else if (auto maybeLoop = NAst::TMaybeNode<NAst::TRepeatStmtExpr>(expr)) {
        co_return co_await LowerRepeat(maybeLoop.Cast(), scope);
    } else if (auto maybeLoop = NAst::TMaybeNode<NAst::TForStmtExpr>(expr)) {
        co_return co_await LowerFor(maybeLoop.Cast(), scope);
    } else if (auto maybeLoop = NAst::TMaybeNode<NAst::TTimesStmtExpr>(expr)) {
        co_return co_await LowerTimes(maybeLoop.Cast(), scope);
    } else if (NAst::TMaybeNode<NAst::TBreakStmt>(expr)) {
        if (!scope.BreakLabel) {
            co_return TError(expr->Location, TErrorString::Get<EErrorId::BREAK_NOT_IN_LOOP>());
        }
        // terminate current block by jumping to the break target
        Builder.Emit0("jmp"_op, {*scope.BreakLabel});
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (NAst::TMaybeNode<NAst::TContinueStmt>(expr)) {
        if (!scope.ContinueLabel) {
            co_return TError(expr->Location, TErrorString::Get<EErrorId::CONTINUE_NOT_IN_LOOP>());
        }
        // terminate current block by jumping to the continue target
        Builder.Emit0("jmp"_op, {*scope.ContinueLabel});
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TArrayAssignExpr>(expr)) {
        auto asg = maybeAsg.Cast();

        auto arrayPtr = co_await LoadVar(asg->Name, scope, asg->Location);
        auto arraySymbol = Context.Lookup(asg->Name, scope.Id);
        if (!arraySymbol) {
            co_return TError(asg->Location, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
        }
        auto arrayType = Builder.GetType(arrayPtr);
        int arrayElemTypeId = Module.Types.UnderlyingType(arrayType);
        assert(arrayElemTypeId >= 0);
        int elemByteSize = Module.Types.SizeInBytes(arrayElemTypeId);
        auto indices = co_await LowerIndices(*arraySymbol, asg->Indices, scope, elemByteSize);
        if (!indices.Value) {
            co_return TError(asg->Location, TErrorString::Get<EErrorId::FAILED_LOWER_ARRAY_INDICES>());
        }
        auto totalIndex = *indices.Value;
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, totalIndex});
        Builder.SetType(destPtr, arrayType);

        auto rhs = co_await Lower(asg->Value, scope);
        if (!rhs.Value) co_return TError(asg->Value->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());

        // copy-paste
        if (rhs.Value->Type == TOperand::EType::Imm) {
            // Materialize immediate string literals into a tmp
            if (rhs.Value->Imm.TypeId == lowStringTypeId) {
                // TODO: create proper kind for string literal
                auto constructorId = co_await GlobalSymbolId("str_from_lit");
                Builder.Emit0("arg"_op, {*rhs.Value});
                auto materializedString = Builder.Emit1("call"_op, {TImm{constructorId}});
                Builder.SetType(materializedString, arrayElemTypeId);
                *rhs.Value = materializedString;
                rhs.Ownership = EOwnership::Owned;
            }
        }

        // copy-paste
        if (arrayElemTypeId == lowStringTypeId && rhs.Ownership == EOwnership::Borrowed) {
            auto retainId = co_await GlobalSymbolId("str_retain");
            Builder.Emit0("arg"_op, {*rhs.Value});
            Builder.Emit0("call"_op, { TImm{ retainId } });
        }

        // copy-paste
        if (arrayElemTypeId == lowStringTypeId) {
            auto dtorId = co_await GlobalSymbolId("str_release");
            auto existingVal = Builder.Emit1("lde"_op, { destPtr });
            Builder.SetType(existingVal, arrayElemTypeId);
            Builder.Emit0("arg"_op, { existingVal });
            Builder.Emit0("call"_op, { TImm{ dtorId } });
        }

        Module.Types.GetKind(arrayElemTypeId);
        if (Module.Types.GetKind(arrayElemTypeId) == EKind::Struct) {
            Builder.Emit0("copy"_op, {destPtr, *rhs.Value, TImm{(int64_t)elemByteSize}});
        } else {
            Builder.Emit0("ste"_op, {destPtr, *rhs.Value});
        }
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeIndex = NAst::TMaybeNode<NAst::TIndexExpr>(expr)) {
        auto index = maybeIndex.Cast();
        auto indexValue = co_await Lower(index->Index, scope);
        if (!indexValue.Value) {
            co_return TError(index->Index->Location, TErrorString::Get<EErrorId::ARRAY_INDEX_NOT_NUMBER>());
        }
        auto value = co_await Lower(index->Collection, scope);
        if (!value.Value) {
            co_return TError(index->Collection->Location, TErrorString::Get<EErrorId::FAILED_LOWER_COLLECTION>());
        }
        auto arrayPtr = *value.Value;
        if (arrayPtr.Type != TOperand::EType::Tmp) {
            co_return TError(index->Collection->Location, TErrorString::Get<EErrorId::COLLECTION_NOT_ARRAY>());
        }

        auto maybeIndexIdent = NAst::TMaybeNode<NAst::TIdentExpr>(index->Collection);
        if (!maybeIndexIdent) {
            co_return TError(index->Collection->Location, TErrorString::Get<EErrorId::COLLECTION_NOT_ARRAY>());
        }
        auto indexSymbol = Context.Lookup(maybeIndexIdent.Cast()->Name, scope.Id);
        if (!indexSymbol) {
            co_return TError(index->Collection->Location, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
        }
        auto layoutIt = ArrayLayouts.find(indexSymbol->Id);
        if (layoutIt == ArrayLayouts.end()) {
            co_return TError(index->Collection->Location, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
        }

        // Adjust index by lower bound: index0 = index - lbound0
        auto lbound0 = LoadLayoutOperand(layoutIt->second.LBounds[0]);
        auto i64 = Module.Types.I(EKind::I64);
        auto zeroBasedIndex = Builder.Emit1("-"_op, {*indexValue.Value, lbound0});
        Builder.SetType(zeroBasedIndex, i64);

        auto arrayType = Builder.GetType(arrayPtr.Tmp);
        int elemTypeId = FromAstType(expr->Type, Module.Types);
        int elemByteSize = Module.Types.SizeInBytes(elemTypeId);
        auto offset = Builder.Emit1("*"_op, {zeroBasedIndex, TImm{elemByteSize, i64}}); // byte offset
        Builder.SetType(offset, i64);
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, offset});
        Builder.SetType(destPtr, arrayType);
        // For struct elements: return the pointer directly (caller uses memcpy/lea semantics)
        bool isStructElem = NAst::TMaybeType<NAst::TStructType>(NAst::UnwrapNamedType(expr->Type));
        if (isStructElem) {
            int ptrType = Module.Types.Ptr(Module.Types.I(EKind::I8));
            Builder.SetType(destPtr, ptrType);
            co_return TValueWithBlock{ destPtr, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
        }
        auto loaded = Builder.Emit1("lde"_op, { destPtr });
        Builder.SetType(loaded, elemTypeId);
        co_return TValueWithBlock{ loaded, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
    } else if (auto maybeConstruct = NAst::TMaybeNode<NAst::TStructConstructExpr>(expr)) {
        auto sc = maybeConstruct.Cast();
        auto objAstType = NAst::UnwrapReferenceType(NAst::UnwrapNamedType(sc->Type));
        int structTypeId = FromAstType(objAstType, Module.Types);
        int64_t totalSize = Module.Types.SizeInBytes(structTypeId);
        int ptrTypeId = Module.Types.Ptr(Module.Types.I(EKind::I64));
        auto i64 = Module.Types.I(EKind::I64);

        auto ptr = Builder.Emit1("salloc"_op, {TImm{totalSize, i64}});
        Builder.SetType(ptr, ptrTypeId);

        const auto& fieldTypes = Module.Types.GetStructFields(structTypeId);
        int64_t offset = 0;
        for (size_t i = 0; i < sc->Fields.size(); ++i) {
            auto fieldVal = co_await Lower(sc->Fields[i], scope);
            if (!fieldVal.Value) {
                co_return TError(sc->Fields[i]->Location, "Не удалось вычислить поле при конструировании структуры.");
            }
            auto fieldPtr = Builder.Emit1("+"_op, {ptr, TImm{offset, i64}});
            Builder.SetType(fieldPtr, ptrTypeId);
            int fieldTypeId = i < fieldTypes.size() ? fieldTypes[i] : -1;
            if (Module.Types.GetKind(fieldTypeId) == EKind::Struct) {
                int fieldSize = Module.Types.SizeInBytes(fieldTypeId);
                Builder.Emit0("copy"_op, {fieldPtr, *fieldVal.Value, TImm{(int64_t)fieldSize}});
            } else {
                Builder.Emit0("ste"_op, {fieldPtr, *fieldVal.Value});
            }
            offset += Module.Types.SizeInBytes(fieldTypeId >= 0 ? fieldTypeId : 8);
        }
        co_return TValueWithBlock{ ptr, Builder.CurrentBlockLabel(), EOwnership::Owned };
    } else if (NAst::TMaybeNode<NAst::TFieldAccessExpr>(expr) || NAst::TMaybeNode<NAst::TFieldAssignExpr>(expr)) {
        auto maybeRead  = NAst::TMaybeNode<NAst::TFieldAccessExpr>(expr);
        auto maybeWrite = NAst::TMaybeNode<NAst::TFieldAssignExpr>(expr);
        NAst::TExprPtr& object = maybeRead ? maybeRead.Cast()->Object : maybeWrite.Cast()->Object;
        int fieldIndex = maybeRead ? maybeRead.Cast()->FieldIndex : maybeWrite.Cast()->FieldIndex;
        const std::string& fieldName = maybeRead ? maybeRead.Cast()->FieldName : maybeWrite.Cast()->FieldName;

        // We need the ADDRESS of the struct, not its value.
        // For a direct variable (TIdentExpr), lea gives the alloca/slot address in both VM and LLVM.
        // For other sub-expressions (e.g. nested field access), Lower already returns a pointer.
        TOperand objAddr;
        if (auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(object)) {
            auto tmp = co_await LoadVar(maybeIdent.Cast()->Name, scope, object->Location,
                                        /*takeRefOfNotRef=*/true);
            objAddr = tmp;
        } else {
            auto res = co_await Lower(object, scope);
            if (!res.Value) {
                co_return TError(object->Location, "Не удалось вычислить объект при обращении к полю '" + fieldName + "'.");
            }
            objAddr = *res.Value;
        }

        // Compute byte offset into the struct
        auto objAstType = NAst::UnwrapReferenceType(NAst::UnwrapNamedType(object->Type));
        int structTypeId = FromAstType(objAstType, Module.Types);
        const auto& fieldTypes = Module.Types.GetStructFields(structTypeId);
        int64_t fieldByteOffset = 0;
        for (int i = 0; i < fieldIndex; ++i) {
            fieldByteOffset += Module.Types.SizeInBytes(fieldTypes[i]);
        }

        auto i64      = Module.Types.I(EKind::I64);
        int  ptrTypeId = Module.Types.Ptr(Module.Types.I(EKind::I8));
        auto fieldPtr  = Builder.Emit1("+"_op, {objAddr, TImm{fieldByteOffset, i64}});
        Builder.SetType(fieldPtr, ptrTypeId);

        // Dispatch: read or write
        NAst::TTypePtr fieldAstType = maybeRead ? expr->Type : maybeWrite.Cast()->Value->Type;
        int fieldTypeId = FromAstType(fieldAstType, Module.Types);
        if (maybeRead) {
            if (Module.Types.GetKind(fieldTypeId) == EKind::Struct) {
                co_return TValueWithBlock{ fieldPtr, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
            }
            auto loaded = Builder.Emit1("lde"_op, { fieldPtr });
            Builder.SetType(loaded, fieldTypeId);
            co_return TValueWithBlock{ loaded, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
        } else {
            auto rhs = co_await Lower(maybeWrite.Cast()->Value, scope);
            if (!rhs.Value) {
                co_return TError(maybeWrite.Cast()->Value->Location, "Не удалось вычислить значение при присваивании полю '" + fieldName + "'.");
            }
            if (Module.Types.GetKind(fieldTypeId) == EKind::Struct) {
                Builder.Emit0("copy"_op, {fieldPtr, *rhs.Value, TImm{(int64_t)Module.Types.SizeInBytes(fieldTypeId)}});
            } else {
                Builder.Emit0("ste"_op, {fieldPtr, *rhs.Value});
            }
            co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
        }
    } else if (auto maybeMultiIndex = NAst::TMaybeNode<NAst::TMultiIndexExpr>(expr)) {
        auto multiIndex = maybeMultiIndex.Cast();
        auto maybeIdent = NAst::TMaybeNode<NAst::TIdentExpr>(multiIndex->Collection);
        if (!maybeIdent) {
            co_return TError(multiIndex->Collection->Location, TErrorString::Get<EErrorId::MULTI_INDEX_COLLECTION_MUST_BE_IDENTIFIER>());
        }
        auto asg = maybeIdent.Cast();
        auto arraySymbol = Context.Lookup(asg->Name, scope.Id);
        if (!arraySymbol) {
            co_return TError(multiIndex->Collection->Location, TErrorString::Get<EErrorId::UNDEFINED_NAME>());
        }
        auto value = co_await Lower(multiIndex->Collection, scope);
        if (!value.Value) {
            co_return TError(multiIndex->Collection->Location, TErrorString::Get<EErrorId::FAILED_LOWER_COLLECTION>());
        }
        auto arrayPtr = *value.Value;
        if (arrayPtr.Type != TOperand::EType::Tmp) {
            co_return TError(multiIndex->Collection->Location, TErrorString::Get<EErrorId::COLLECTION_NOT_ARRAY>());
        }
        auto arrayType = Builder.GetType(arrayPtr.Tmp);
        int multiElemTypeId = FromAstType(expr->Type, Module.Types);
        int multiElemByteSize = Module.Types.SizeInBytes(multiElemTypeId);
        auto indices = co_await LowerIndices(*arraySymbol, multiIndex->Indices, scope, multiElemByteSize);
        if (!indices.Value) {
            co_return TError(asg->Location, TErrorString::Get<EErrorId::FAILED_LOWER_ARRAY_INDICES>());
        }
        auto totalIndex = *indices.Value;
        auto destPtr = Builder.Emit1("+"_op, {arrayPtr, totalIndex});
        Builder.SetType(destPtr, arrayType);
        auto loaded = Builder.Emit1("lde"_op, { destPtr });
        Builder.SetType(loaded, FromAstType(expr->Type, Module.Types));
        co_return TValueWithBlock{ loaded, Builder.CurrentBlockLabel(), EOwnership::Borrowed };
    } else if (auto maybeAsg = NAst::TMaybeNode<NAst::TAssignExpr>(expr)) {
        auto asg = maybeAsg.Cast();
        auto rhs = co_await Lower(asg->Value, scope);
        if (!rhs.Value) co_return TError(asg->Value->Location, TErrorString::Get<EErrorId::RIGHT_HAND_SIDE_NOT_NUMBER>());
        auto sidOpt = Context.Lookup(asg->Name, scope.Id);
        if (!sidOpt) co_return TError(asg->Location, TErrorString::Get<EErrorId::ASSIGNMENT_TO_UNDEFINED>());

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

        auto nodeType = NAst::UnwrapNamedType(node->Type);

        // TODO: copy-paste
        if (NAst::TMaybeType<NAst::TStringType>(nodeType)) {
            if (rhs.Ownership == EOwnership::Borrowed) {
                auto retainId = co_await GlobalSymbolId("str_retain");
                Builder.Emit0("arg"_op, {*rhs.Value});
                Builder.Emit0("call"_op, { TImm{retainId} });
            }
        }
        {
            if (NAst::TMaybeType<NAst::TStringType>(nodeType) && !NAst::TMaybeType<NAst::TReferenceType>(nodeType)) {
                auto dtorId = co_await GlobalSymbolId("str_release");
                TTmp currentVal = Builder.Emit1("load"_op, {storeOperand});
                Builder.SetType(currentVal, slotType);
                Builder.Emit0("arg"_op, { currentVal });
                Builder.Emit0("call"_op, { TImm{dtorId} });
            }
        }

        if (auto maybeRef = NAst::TMaybeType<NAst::TReferenceType>(nodeType)) {
            auto ref = maybeRef.Cast();
            auto addrTmp = Builder.Emit1("load"_op, {storeOperand});
            Builder.SetType(addrTmp, FromAstType(node->Type, Module.Types));

            auto refType = NAst::UnwrapNamedType(ref->ReferencedType);
            if (NAst::TMaybeType<NAst::TStructType>(refType).Cast() != nullptr) {
                // рез компл: addrTmp is Tmp pointer to destination, rhs is Tmp pointer to source
                int sizeBytes = Module.Types.SizeInBytes(FromAstType(refType, Module.Types));
                Builder.Emit0("copy"_op, {addrTmp, *rhs.Value, TImm{(int64_t)sizeBytes}});
            } else {
                // see cases/ref/index_ref
                if (NAst::TMaybeType<NAst::TStringType>(refType)) {
                    auto retainId = co_await GlobalSymbolId("str_retain");
                    Builder.Emit0("arg"_op, {*rhs.Value});
                    Builder.Emit0("call"_op, { TImm{retainId} });

                    auto prevVal = Builder.Emit1("lde"_op, { addrTmp });
                    Builder.SetType(prevVal, FromAstType(ref->ReferencedType, Module.Types));
                    auto dtorId = co_await GlobalSymbolId("str_release");
                    Builder.Emit0("arg"_op, { prevVal });
                    Builder.Emit0("call"_op, { TImm{ dtorId } });
                }
                Builder.Emit0("ste"_op, {addrTmp, *rhs.Value});
            }
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
            auto refType = NAst::UnwrapNamedType(ref->ReferencedType);
            if (NAst::TMaybeType<NAst::TStructType>(refType)) {
                Builder.SetType(tmp, Module.Types.Ptr(Module.Types.I(EKind::I8)));
            } else {
                auto derefTmp = Builder.Emit1("lde"_op, { tmp });
                Builder.SetType(derefTmp, FromAstType(ref->ReferencedType, Module.Types));
                tmp = derefTmp;
            }
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
            co_return TError(var->Location, TErrorString::Get<EErrorId::VAR_HAS_NO_BINDING>());
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
                .FunctionId = { dtorId }
            };
            PendingDestructors.emplace_back(std::move(dtor));
        }
        if (auto maybeArrayType = NAst::TMaybeType<NAst::TArrayType>(var->Type)) {
            auto arrayType = FromAstType(var->Type, Module.Types);
            auto elemType = Module.Types.UnderlyingType(arrayType);
            auto elemSize = Module.Types.SizeInBytes(elemType);
            auto ctorId = co_await GlobalSymbolId("array_create");

            auto layout = co_await LowerArrayLayout(*sidOpt, var->Bounds, scope, var->Location);
            auto tmp = LoadLayoutOperand(layout.TotalElements);
            auto i64 = Module.Types.I(EKind::I64);
            auto arraySize = Builder.Emit1("*"_op, {tmp, TImm{elemSize, i64}});
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
                .FunctionId = { dtorId }
            };
            PendingDestructors.emplace_back(std::move(dtor));
        }
        co_return TValueWithBlock{ std::nullopt, Builder.CurrentBlockLabel() };
    } else if (auto maybeFun = NAst::TMaybeNode<NAst::TFunDecl>(expr)) {
        auto fun = maybeFun.Cast();
        auto name = fun->Name;
        if (scope.Id.Id != 0) {
            co_return TError(fun->Location, TErrorString::Get<EErrorId::NESTED_FUNCTIONS_NOT_SUPPORTED>());
        }
        auto params = fun->Params;
        auto body = fun->Body;
        auto type = NAst::TMaybeType<NAst::TFunctionType>(fun->Type).Cast();
        // Explicit symbol lookup (synchronous) instead of co_await on optionals.
        // Prefer direct decl binding (robust for nested functions where scope.Id may not be the decl scope).
        auto sidOpt = Context.Lookup(fun->Name, scope.Id);
        if (!sidOpt) {
            co_return TError(fun->Location, TErrorString::Get<EErrorId::UNBOUND_FUNCTION_SYMBOL>(fun->Name, scope.Id.Id));
        }
        auto funScope = fun->Body->Scope;
        auto functionScope = fun->Scope;
        std::vector<TLocal> args; args.reserve(params.size());
        int i = 0;
        for (auto& p : params) {
            auto psid = Context.Lookup(p->Name, NSemantics::TScopeId{funScope});
            if (!psid) {
                co_return TError(p->Location, TErrorString::Get<EErrorId::PARAMETER_NO_BINDING>());
            }
            auto local = TLocal{ psid->FunctionLevelIdx };
            args.push_back(local);
            i++;
        }

        // auto currentFuncIdx = Builder.CurrentFunctionIdx(); // needed for nested functions
        auto funcIdx = Builder.NewFunction(name, args, sidOpt->Id);
        auto coroutineResultType = NAst::FutureResultType(fun->RetType);
        const bool isCoroutine = static_cast<bool>(coroutineResultType);
        auto physicalReturnAstType = isCoroutine
            ? std::make_shared<NAst::TPointerType>(std::make_shared<NAst::TVoidType>())
            : fun->RetType;
        auto returnType = FromAstType(physicalReturnAstType, Module.Types);
        Builder.SetReturnType(returnType);
        Module.Functions[funcIdx].IsCoroutine = isCoroutine;
        if (isCoroutine) {
            Module.Functions[funcIdx].CoroutineResultTypeId = FromAstType(coroutineResultType, Module.Types);
        }
        for (auto& a : args) {
            Builder.SetType(a, FromAstType(type->ParamTypes[&a - &args[0]], Module.Types));
        }
        int localCount = 0;
        for (const auto& symbol : Context.GetSymbols()) {
            if (symbol.FuncScopeId.Id == functionScope && symbol.FunctionLevelIdx >= 0) {
                localCount = std::max(localCount, symbol.FunctionLevelIdx + 1);
            }
        }
        Builder.ReserveLocals(localCount);
        if (NAst::TMaybeType<NAst::TStringType>(fun->RetType)) {
            // TODO: remove me
            // clutch: support string returnType
            Module.Functions[Builder.CurrentFunctionIdx()].ReturnTypeIsString = true;
        }

        TBlockScope functionBodyScope {
            .FuncIdx = funcIdx,
            .Id = NSemantics::TScopeId{funScope},
            .BreakLabel = std::nullopt,
            .ContinueLabel = std::nullopt
        };
        for (auto& param : fun->Params) {
            if (param->Bounds.empty() || !NAst::TMaybeType<NAst::TArrayType>(param->Type)) {
                continue;
            }
            auto psid = Context.Lookup(param->Name, NSemantics::TScopeId{funScope});
            if (!psid) {
                co_return TError(param->Location, TErrorString::Get<EErrorId::PARAMETER_NO_BINDING>());
            }
            co_await LowerArrayLayout(*psid, param->Bounds, functionBodyScope, param->Location);
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
        if (fun->LastAssert) {
            co_await Lower(fun->LastAssert, TBlockScope {
                .FuncIdx = funcIdx,
                .Id = NSemantics::TScopeId{funScope},
                .BreakLabel = std::nullopt,
                .ContinueLabel = std::nullopt
            });
        }
        if (isCoroutine && NAst::TMaybeType<NAst::TVoidType>(coroutineResultType)) {
            Builder.Emit0("ret"_op, {});
        } else if (!NAst::TMaybeType<NAst::TVoidType>(isCoroutine ? coroutineResultType : fun->RetType)) {
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
                if (!sidOpt) co_return TError(ident->Location, TErrorString::Get<EErrorId::UNBOUND_FUNCTION_SYMBOL>(ident->Name, scope.Id.Id));
            calleeSymId = sidOpt->Id;
        } else {
            co_return TError(call->Callee->Location, TErrorString::Get<EErrorId::FUNCTION_CALL_NON_IDENTIFIER>());
        }

        auto funDecl = NAst::TMaybeNode<NAst::TFunDecl>(Context.GetSymbolNode(NSemantics::TSymbolId{calleeSymId})).Cast();
        if (!funDecl) {
            co_return TError(call->Callee->Location, TErrorString::Get<EErrorId::NOT_A_FUNCTION>());
        }
        NAst::TTypePtr returnType = PhysicalCallResultType(funDecl->RetType);
        std::vector<NAst::TTypePtr>* argTypes = nullptr;
        if (auto maybeFuncType = NAst::TMaybeType<NAst::TFunctionType>(funDecl->Type)) {
            argTypes = &maybeFuncType.Cast()->ParamTypes;
        }

        bool isExternalCall = funDecl->IsExternal();
        if (isExternalCall && !Module.SymIdToExtFuncIdx.contains(calleeSymId)) {
            ImportExternalFunction(calleeSymId, *funDecl);
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
                    co_return TError(a->Location, TErrorString::Get<EErrorId::ARG_REF_MUST_BE_IDENTIFIER>());
                }
                av.Value = co_await LoadVar(maybeIdent.Cast()->Name, scope, a->Location, true /*address*/);
                Builder.SetType(av.Value->Tmp, FromAstType(argType, Module.Types));
            } else {
                // struct args and scalar args are both lowered the same way —
                // VMCompiler handles ABI (lea/pointer) for struct types
                av = co_await Lower(a, scope);
            }

            if (!av.Value) co_return TError(a->Location, TErrorString::Get<EErrorId::INVALID_ARGUMENT>());

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
            tmp = Builder.Emit1(isAwait ? "await"_op : "call"_op, {TImm{calleeSymId}});
            Builder.SetType(tmp->Tmp, FromAstType(returnType, Module.Types));
        } else {
            Builder.Emit0(isAwait ? "await"_op : "call"_op, {TImm{calleeSymId}});
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
        oss << expr;
        co_return TError(expr->Location, TErrorString::Get<EErrorId::NOT_IMPLEMENTED_LOWERING>(oss.str()));
    }
}

void TAstLowerer::ImportExternalFunction(int symbolId, const NAst::TFunDecl& funcDecl) {
    if (Module.SymIdToExtFuncIdx.find(symbolId) != Module.SymIdToExtFuncIdx.end()) {
        // already imported
        return;
    }
    // Pure inline functions have no physical implementation — skip registration
    // so backends (LLVM, WASM) don't emit imports or declarations for them.
    if (funcDecl.InlineFactory.has_value()) {
        return;
    }

    std::vector<int> argTypes; argTypes.reserve(funcDecl.Params.size());
    int returnType = FromAstType(PhysicalCallResultType(funcDecl.RetType), Module.Types);
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
    co_return TError({}, TErrorString::Get<EErrorId::UNDEFINED_GLOBAL_SYMBOL>(name));
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
        return std::unexpected(TError(expr->Location, TErrorString::Get<EErrorId::ROOT_EXPR_MUST_BE_BLOCK>()));
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
            } else if (auto maybeSeq = NAst::TMaybeNode<NAst::TSeqExpr>(s)) {
                for (auto& stmt : maybeSeq.Cast()->Stmts) {
                    auto res = lowerTopBlock(std::make_shared<NAst::TBlockExpr>(
                        maybeSeq.Cast()->Location,
                        std::vector<NAst::TExprPtr>{stmt}), scope);
                    if (!res) {
                        return std::unexpected(res.error());
                    }
                }
            } else if (auto maybeVar = NAst::TMaybeNode<NAst::TVarStmt>(s)) {
                if (functionSeen) {
                    return std::unexpected(TError(s->Location, TErrorString::Get<EErrorId::VARIABLE_DECLS_BEFORE_FUNS>()));
                }
                auto var = maybeVar.Cast();
                auto sidOpt = Context.Lookup(var->Name, scope.Id);
                if (!sidOpt) {
                    return std::unexpected(TError(var->Location, TErrorString::Get<EErrorId::VAR_HAS_NO_BINDING>()));
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
                        return std::unexpected(TError(s->Location, TErrorString::Get<EErrorId::VARIABLE_DECLS_BEFORE_FUNS>()));
                    }
                auto asg = maybeAsg.Cast();
                auto maybeNumber = NAst::TMaybeNode<NAst::TNumberExpr>(asg->Value);
                auto sid = Context.Lookup(asg->Name, scope.Id);
                if (!sid) {
                    return std::unexpected(TError(s->Location, TErrorString::Get<EErrorId::UNDEFINED_VARIABLE>(asg->Name)));
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
            } else if (NAst::TMaybeNode<NAst::TUseExpr>(s)) {
                // Imports are handled during parsing/name resolution; keep the AST node
                // for source/core printing but do not lower it to IR.
            } else {
                return std::unexpected(TError(s->Location, TErrorString::Get<EErrorId::UNEXPECTED_TOP_LEVEL_STATEMENT>(s->ToString())));
            }
        }

        return {};
    };

    if (auto res = lowerTopBlock(block, scope); !res) {
        return res;
    }
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
