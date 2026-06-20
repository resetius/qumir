#include "pass.h"

namespace NQumir {
namespace NSemantics {
namespace {

using namespace NAst;

enum class EValueOwnership {
    NotApplicable,
    Borrowed,
    Owned,
    Literal,
};

TTypePtr ValueType(TTypePtr type) {
    while (true) {
        if (auto reference = TMaybeType<TReferenceType>(type)) {
            type = reference.Cast()->ReferencedType;
        } else if (auto named = TMaybeType<TNamedType>(type)) {
            type = named.Cast()->UnderlyingType;
        } else {
            return type;
        }
    }
}

bool IsString(const TTypePtr& type) {
    return static_cast<bool>(TMaybeType<TStringType>(ValueType(type)));
}

std::expected<EValueOwnership, TError> ClassifyOwnership(const TExprPtr& expr) {
    if (!expr || !IsString(expr->Type)) {
        return EValueOwnership::NotApplicable;
    }
    if (TMaybeNode<TStringLiteralExpr>(expr)) {
        return EValueOwnership::Literal;
    }
    if (TMaybeNode<TIdentExpr>(expr)
        || TMaybeNode<TIndexExpr>(expr)
        || TMaybeNode<TMultiIndexExpr>(expr)
        || TMaybeNode<TFieldAccessExpr>(expr)
        || TMaybeNode<TBorrowExpr>(expr))
    {
        return EValueOwnership::Borrowed;
    }
    if (TMaybeNode<TCallExpr>(expr)
        || TMaybeNode<TAwaitExpr>(expr)
        || TMaybeNode<TRetainExpr>(expr)
        || TMaybeNode<TOwnLiteralExpr>(expr)
        || TMaybeNode<TMoveExpr>(expr))
    {
        return EValueOwnership::Owned;
    }
    return std::unexpected(TError(
        expr->Location,
        "Cannot determine ownership of string expression '"
            + std::string(expr->NodeName()) + "'."));
}

std::expected<TExprPtr, TError> RequireOwned(TExprPtr value) {
    auto ownership = ClassifyOwnership(value);
    if (!ownership) {
        return std::unexpected(ownership.error());
    }
    switch (*ownership) {
    case EValueOwnership::Literal:
        return std::make_shared<TOwnLiteralExpr>(value->Location, std::move(value));
    case EValueOwnership::Borrowed:
        return std::make_shared<TRetainExpr>(
            value->Location,
            std::make_shared<TBorrowExpr>(value->Location, std::move(value)));
    case EValueOwnership::Owned:
        return std::make_shared<TMoveExpr>(value->Location, std::move(value));
    case EValueOwnership::NotApplicable:
        return std::unexpected(TError(
            value->Location,
            "Owned string value was required for a non-string expression."));
    }
    return std::unexpected(TError(value->Location, "Unknown string ownership."));
}

TExprPtr MakeNullString(const TLocation& location, const TTypePtr& type) {
    auto zero = std::make_shared<TNumberExpr>(location, int64_t{0});
    zero->Type = std::make_shared<TIntegerType>();
    return std::make_shared<TBitcastExpr>(location, std::move(zero), type);
}

bool IsNullString(const TExprPtr& expr) {
    if (auto number = TMaybeNode<TNumberExpr>(expr)) {
        return IsString(number.Cast()->Type)
            && !number.Cast()->IsFloat()
            && number.Cast()->IntValue == 0;
    }
    auto bitcast = TMaybeNode<TBitcastExpr>(expr);
    if (!bitcast || !IsString(bitcast.Cast()->Type)) {
        return false;
    }
    auto number = TMaybeNode<TNumberExpr>(bitcast.Cast()->Operand);
    return number && !number.Cast()->IsFloat() && number.Cast()->IntValue == 0;
}

class TStringAssignmentRewriter {
public:
    explicit TStringAssignmentRewriter(TNameResolver& context)
        : Context_(context)
    {}

    std::expected<bool, TError> Rewrite(TExprPtr& root) {
        return RewriteExpr(root, TScopeId{0});
    }

private:
    std::expected<bool, TError> RewriteBlock(
        const std::shared_ptr<TBlockExpr>& block,
        TScopeId parentScope)
    {
        auto scopeId = block->Scope >= 0
            ? TScopeId{block->Scope}
            : parentScope;
        bool changed = false;
        std::vector<TExprPtr> statements;
        statements.reserve(block->Stmts.size());
        for (auto& statement : block->Stmts) {
            if (auto variable = TMaybeNode<TVarStmt>(statement);
                variable && IsString(variable.Cast()->Type))
            {
                if (IsNullString(variable.Cast()->Init)) {
                    statements.push_back(statement);
                    continue;
                }
                auto init = std::move(variable.Cast()->Init);
                variable.Cast()->Init = MakeNullString(
                    variable.Cast()->Location,
                    variable.Cast()->Type);
                statements.push_back(statement);
                changed = true;
                if (init) {
                    if (auto result = RewriteExpr(init, scopeId); !result) {
                        return std::unexpected(result.error());
                    } else {
                        changed = result.value() || changed;
                    }
                    auto owned = RequireOwned(std::move(init));
                    if (!owned) {
                        return std::unexpected(owned.error());
                    }
                    auto target = std::make_shared<TIdentExpr>(
                        variable.Cast()->Location,
                        variable.Cast()->Name);
                    target->Type = variable.Cast()->Type;
                    statements.push_back(std::make_shared<TReplaceExpr>(
                        variable.Cast()->Location,
                        std::move(target),
                        std::move(owned.value())));
                }
                continue;
            }

            if (auto result = RewriteExpr(statement, scopeId); !result) {
                return std::unexpected(result.error());
            } else {
                changed = result.value() || changed;
            }
            statements.push_back(statement);
        }
        block->Stmts = std::move(statements);
        return changed;
    }

    std::expected<bool, TError> RewriteAssign(
        TExprPtr& expr,
        const std::shared_ptr<TAssignExpr>& assign,
        TScopeId scopeId)
    {
        bool changed = false;
        if (auto result = RewriteExpr(assign->Value, scopeId); !result) {
            return std::unexpected(result.error());
        } else {
            changed = result.value();
        }
        auto symbol = Context_.Lookup(assign->Name, scopeId);
        if (!symbol) {
            return std::unexpected(TError(
                assign->Location,
                "Cannot resolve assignment target '" + assign->Name + "'."));
        }
        auto declaration = Context_.GetSymbolNode(TSymbolId{symbol->Id});
        if (!declaration || !IsString(declaration->Type)) {
            return changed;
        }
        auto owned = RequireOwned(std::move(assign->Value));
        if (!owned) {
            return std::unexpected(owned.error());
        }
        auto target = std::make_shared<TIdentExpr>(assign->Location, assign->Name);
        target->Type = declaration->Type;
        expr = std::make_shared<TReplaceExpr>(
            assign->Location,
            std::move(target),
            std::move(owned.value()));
        return true;
    }

    std::expected<bool, TError> RewriteArrayAssign(
        TExprPtr& expr,
        const std::shared_ptr<TArrayAssignExpr>& assign,
        TScopeId scopeId)
    {
        bool changed = false;
        for (auto& index : assign->Indices) {
            auto result = RewriteExpr(index, scopeId);
            if (!result) {
                return std::unexpected(result.error());
            }
            changed = result.value() || changed;
        }
        if (auto result = RewriteExpr(assign->Value, scopeId); !result) {
            return std::unexpected(result.error());
        } else {
            changed = result.value() || changed;
        }

        auto symbol = Context_.Lookup(assign->Name, scopeId);
        if (!symbol) {
            return std::unexpected(TError(
                assign->Location,
                "Cannot resolve indexed assignment target '" + assign->Name + "'."));
        }
        auto declaration = Context_.GetSymbolNode(TSymbolId{symbol->Id});
        auto collectionType = declaration
            ? ValueType(declaration->Type)
            : nullptr;
        TTypePtr elementType;
        if (auto array = TMaybeType<TArrayType>(collectionType)) {
            elementType = array.Cast()->ElementType;
        } else if (auto pointer = TMaybeType<TPointerType>(collectionType)) {
            elementType = pointer.Cast()->PointeeType;
        }
        if (!IsString(elementType)) {
            return changed;
        }

        auto collection = std::make_shared<TIdentExpr>(assign->Location, assign->Name);
        collection->Type = declaration->Type;
        TExprPtr target;
        if (assign->Indices.size() == 1) {
            target = std::make_shared<TIndexExpr>(
                assign->Location,
                std::move(collection),
                std::move(assign->Indices.front()));
        } else {
            target = std::make_shared<TMultiIndexExpr>(
                assign->Location,
                std::move(collection),
                std::move(assign->Indices));
        }
        target->Type = elementType;
        auto owned = RequireOwned(std::move(assign->Value));
        if (!owned) {
            return std::unexpected(owned.error());
        }
        expr = std::make_shared<TReplaceExpr>(
            assign->Location,
            std::move(target),
            std::move(owned.value()));
        return true;
    }

    std::expected<bool, TError> RewriteFieldAssign(
        TExprPtr& expr,
        const std::shared_ptr<TFieldAssignExpr>& assign,
        TScopeId scopeId)
    {
        bool changed = false;
        if (auto result = RewriteExpr(assign->Object, scopeId); !result) {
            return std::unexpected(result.error());
        } else {
            changed = result.value() || changed;
        }
        if (auto result = RewriteExpr(assign->Value, scopeId); !result) {
            return std::unexpected(result.error());
        } else {
            changed = result.value() || changed;
        }

        auto objectType = ValueType(assign->Object->Type);
        auto structType = TMaybeType<TStructType>(objectType);
        if (!structType
            || assign->FieldIndex < 0
            || static_cast<size_t>(assign->FieldIndex) >= structType.Cast()->Fields.size())
        {
            return changed;
        }
        auto fieldType = structType.Cast()->Fields[assign->FieldIndex].second;
        if (!IsString(fieldType)) {
            return changed;
        }
        auto target = std::make_shared<TFieldAccessExpr>(
            assign->Location,
            std::move(assign->Object),
            assign->FieldName);
        target->FieldIndex = assign->FieldIndex;
        target->Type = fieldType;
        auto owned = RequireOwned(std::move(assign->Value));
        if (!owned) {
            return std::unexpected(owned.error());
        }
        expr = std::make_shared<TReplaceExpr>(
            assign->Location,
            std::move(target),
            std::move(owned.value()));
        return true;
    }

    std::expected<bool, TError> RewriteExpr(TExprPtr& expr, TScopeId scopeId) {
        if (!expr) {
            return false;
        }
        if (auto block = TMaybeNode<TBlockExpr>(expr)) {
            return RewriteBlock(block.Cast(), scopeId);
        }
        if (auto function = TMaybeNode<TFunDecl>(expr)) {
            TExprPtr body = function.Cast()->Body;
            auto result = RewriteExpr(body, TScopeId{function.Cast()->Scope});
            function.Cast()->Body = std::static_pointer_cast<TBlockExpr>(body);
            return result;
        }
        if (auto assign = TMaybeNode<TAssignExpr>(expr)) {
            return RewriteAssign(expr, assign.Cast(), scopeId);
        }
        if (auto assign = TMaybeNode<TArrayAssignExpr>(expr)) {
            return RewriteArrayAssign(expr, assign.Cast(), scopeId);
        }
        if (auto assign = TMaybeNode<TFieldAssignExpr>(expr)) {
            return RewriteFieldAssign(expr, assign.Cast(), scopeId);
        }

        bool changed = false;
        for (auto* child : expr->MutableChildren()) {
            if (!*child) {
                continue;
            }
            auto result = RewriteExpr(*child, scopeId);
            if (!result) {
                return std::unexpected(result.error());
            }
            changed = result.value() || changed;
        }
        return changed;
    }

    TNameResolver& Context_;
};

} // namespace

std::expected<bool, TError> LifetimePass(
    NAst::TExprPtr& expr,
    TNameResolver& context,
    TSyntheticNameGenerator& syntheticNames)
{
    (void)syntheticNames;
    return TStringAssignmentRewriter(context).Rewrite(expr);
}

} // namespace NSemantics
} // namespace NQumir
