#include "synthetic_name_generator.h"

namespace NQumir {
namespace NSemantics {
namespace {

void CollectAstNames(
    const NAst::TExprPtr& expr,
    std::unordered_set<std::string>& names)
{
    if (!expr) {
        return;
    }
    if (auto var = NAst::TMaybeNode<NAst::TVarStmt>(expr)) {
        names.insert(var.Cast()->Name);
    } else if (auto function = NAst::TMaybeNode<NAst::TFunDecl>(expr)) {
        names.insert(function.Cast()->Name);
    } else if (auto typeDecl = NAst::TMaybeNode<NAst::TTypeDeclStmt>(expr)) {
        if (auto named = NAst::TMaybeType<NAst::TNamedType>(typeDecl.Cast()->Type)) {
            names.insert(named.Cast()->Name);
        }
    }
    for (const auto& child : expr->Children()) {
        CollectAstNames(child, names);
    }
}

} // namespace

TSyntheticNameGenerator::TSyntheticNameGenerator(
    const TNameResolver& context,
    const NAst::TExprPtr& root)
{
    for (const auto& symbol : context.GetSymbols()) {
        ReservedNames.insert(symbol.Name);
    }
    for (const auto& typeName : context.GetAllImportedTypeNames()) {
        ReservedNames.insert(typeName);
    }
    CollectAstNames(root, ReservedNames);
}

std::string TSyntheticNameGenerator::Next() {
    while (true) {
        auto candidate = "__lifetime_" + std::to_string(NextId++);
        if (ReservedNames.insert(candidate).second) {
            return candidate;
        }
    }
}

} // namespace NSemantics
} // namespace NQumir
