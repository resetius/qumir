#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/ir/builder.h>

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

using namespace NQumir;
using namespace NQumir::NIR::NLiterals;

namespace {

void AddFunction(
    NIR::TModule& module,
    const std::string& name,
    int symId,
    size_t size,
    bool inlineFunction = false)
{
    NIR::TFunction function{};
    function.Name = name;
    function.SymId = symId;
    function.Inline = inlineFunction;
    function.Blocks.emplace_back();
    function.Blocks.back().Instrs.assign(size, NIR::TInstr{.Op = "nop"_op});
    module.Functions.push_back(std::move(function));
}

void Call(NIR::TModule& module, size_t caller, int callee) {
    NIR::TInstr instruction{.Op = "call"_op};
    instruction.OperandCount = 1;
    instruction.Operands[0] = NIR::TImm{callee};
    module.Functions[caller].Blocks.front().Instrs.push_back(instruction);
}

} // namespace

TEST(InlineImports, ReachableHotBodiesAndSmallHelpers) {
    NIR::TModule module;
    AddFunction(module, "kernel", 1, 1);
    AddFunction(module, "hot", 2, 512, true);
    AddFunction(module, "small", 3, 2);
    AddFunction(module, "cold", 4, 512);
    AddFunction(module, "cold_hot", 5, 512, true);
    AddFunction(module, "unused", 6, 512, true);
    Call(module, 0, 2);
    Call(module, 0, 4);
    Call(module, 1, 3);
    Call(module, 3, 5);
    const std::unordered_set<std::string> cacheable{
        "hot", "small", "cold", "cold_hot", "unused"};
    EXPECT_EQ(NCodeGen::CollectInlineDefinitions(module, cacheable, {"kernel"}),
        (std::unordered_set<std::string>{"hot", "small"}));
}

TEST(InlineImports, DependencyObjectHasItsOwnImportClosure) {
    NIR::TModule module;
    AddFunction(module, "cold", 1, 512);
    AddFunction(module, "leaf", 2, 2, true);
    AddFunction(module, "unrelated", 3, 2, true);
    Call(module, 0, 2);
    EXPECT_EQ(NCodeGen::CollectInlineDefinitions(module, {"cold", "leaf", "unrelated"}, {"cold"}),
        (std::unordered_set<std::string>{"leaf"}));
}

TEST(InlineImports, RecursiveHelpersTerminateTraversal) {
    NIR::TModule module;
    AddFunction(module, "kernel", 1, 1);
    AddFunction(module, "a", 2, 2, true);
    AddFunction(module, "b", 3, 2, true);
    Call(module, 0, 2);
    Call(module, 1, 3);
    Call(module, 2, 2);
    EXPECT_EQ(NCodeGen::CollectInlineDefinitions(module, {"a", "b"}, {"kernel"}),
        (std::unordered_set<std::string>{"a", "b"}));
}

TEST(InlineImports, AutomaticBudgetDoesNotSuppressExplicitInline) {
    NIR::TModule module;
    AddFunction(module, "kernel", 1, 0);
    std::unordered_set<std::string> cacheable;
    for (int i = 0; i < 32; ++i) {
        const std::string name = "helper" + std::to_string(i);
        AddFunction(module, name, i + 2, 256);
        cacheable.insert(name);
        Call(module, 0, i + 2);
    }
    AddFunction(module, "explicit", 100, 512, true);
    cacheable.insert("explicit");
    Call(module, 0, 100);
    const auto imports = NCodeGen::CollectInlineDefinitions(module, cacheable, {"kernel"});
    EXPECT_EQ(imports.size(), 17u);
    EXPECT_TRUE(imports.contains("helper0"));
    EXPECT_FALSE(imports.contains("helper31"));
    EXPECT_TRUE(imports.contains("explicit"));
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
