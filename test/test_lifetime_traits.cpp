#include <gtest/gtest.h>

#include <qumir/semantics/lifetime/type_traits.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace NQumir;
using namespace NQumir::NAst;
using namespace NQumir::NSemantics;

namespace {

struct TUnknownManagedType : TType {
    static constexpr const char* TypeId = "UnknownManaged";

    const std::string_view TypeName() const override {
        return TypeId;
    }
};

void ExpectTraits(
    const TTypePtr& type,
    ELifetimeKind kind,
    bool canCopy,
    bool needsDestroy)
{
    const auto traits = GetLifetimeTraits(type);
    EXPECT_EQ(traits.Kind, kind);
    EXPECT_EQ(traits.CanCopy, canCopy);
    EXPECT_EQ(traits.NeedsDestroy, needsDestroy);
}

} // namespace

TEST(LifetimeTraits, ScalarTypesAreTrivial) {
    ExpectTraits(std::make_shared<TIntegerType>(), ELifetimeKind::Trivial, true, false);
    ExpectTraits(std::make_shared<TFloatType>(), ELifetimeKind::Trivial, true, false);
    ExpectTraits(std::make_shared<TBoolType>(), ELifetimeKind::Trivial, true, false);
    ExpectTraits(std::make_shared<TSymbolType>(), ELifetimeKind::Trivial, true, false);
    ExpectTraits(
        std::make_shared<TPointerType>(std::make_shared<TStringType>()),
        ELifetimeKind::Trivial,
        true,
        false);
}

TEST(LifetimeTraits, StringIsRefCounted) {
    ExpectTraits(std::make_shared<TStringType>(), ELifetimeKind::RefCounted, true, true);
}

TEST(LifetimeTraits, NamedStringUsesUnderlyingType) {
    auto type = std::make_shared<TNamedType>("Text", std::make_shared<TStringType>());
    ExpectTraits(type, ELifetimeKind::RefCounted, true, true);
}

TEST(LifetimeTraits, StringReferenceIsBorrowed) {
    auto type = std::make_shared<TReferenceType>(std::make_shared<TStringType>());
    ExpectTraits(type, ELifetimeKind::Trivial, true, false);
}

TEST(LifetimeTraits, PlainArrayIsUnique) {
    auto type = std::make_shared<TArrayType>(std::make_shared<TIntegerType>(), 1);
    ExpectTraits(type, ELifetimeKind::Unique, false, true);
}

TEST(LifetimeTraits, StringArrayIsUnique) {
    auto type = std::make_shared<TArrayType>(std::make_shared<TStringType>(), 1);
    ExpectTraits(type, ELifetimeKind::Unique, false, true);
}

TEST(LifetimeTraits, TrivialStructIsCopyableAggregate) {
    auto type = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"number", std::make_shared<TIntegerType>()},
            {"flag", std::make_shared<TBoolType>()},
        });
    ExpectTraits(type, ELifetimeKind::Aggregate, true, false);
}

TEST(LifetimeTraits, StructWithStringNeedsDestroy) {
    auto type = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"value", std::make_shared<TStringType>()},
        });
    ExpectTraits(type, ELifetimeKind::Aggregate, true, true);
}

TEST(LifetimeTraits, StructWithArrayIsNotCopyable) {
    auto type = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"items", std::make_shared<TArrayType>(std::make_shared<TStringType>(), 1)},
        });
    ExpectTraits(type, ELifetimeKind::Aggregate, false, true);
}

TEST(LifetimeTraits, NestedStructPropagatesFieldTraits) {
    auto inner = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"value", std::make_shared<TStringType>()},
        });
    auto outer = std::make_shared<TStructType>(
        std::vector<std::pair<std::string, TTypePtr>>{
            {"inner", std::move(inner)},
        });
    ExpectTraits(outer, ELifetimeKind::Aggregate, true, true);
}

TEST(LifetimeTraits, UnknownManagedShapeIsRejected) {
    auto type = std::make_shared<TUnknownManagedType>();
    EXPECT_THROW(GetLifetimeTraits(type), std::invalid_argument);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
