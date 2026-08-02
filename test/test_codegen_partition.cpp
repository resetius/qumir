#include <gtest/gtest.h>

#include <qumir/codegen/llvm/llvm_codegen.h>
#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/ir/builder.h>
#include <qumir/ir/type.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace NQumir;
using namespace NQumir::NIR;
using namespace NQumir::NIR::NLiterals;

namespace {

// A module with two trivial void functions "A" and "B".
void BuildTwoFuns(NIR::TModule& module) {
    NIR::TBuilder b(module);
    int voidTy = module.Types.I(EKind::Void);
    auto mk = [&](std::string name, int symId) {
        b.NewFunction(std::move(name), {}, symId); // creates the entry block
        b.SetReturnType(voidTy);
        b.Emit0("ret"_op, {});
    };
    mk("A", 1);
    mk("B", 2);
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::vector<std::string> Emit(const NCodeGen::TLLVMCodeGenOptions& opts) {
    NIR::TModule module;
    BuildTwoFuns(module);
    NCodeGen::TLLVMCodeGen cg(opts);
    auto art = cg.Emit(module);
    return art->GetDefinedFunctionNames();
}

} // namespace

TEST(CodegenPartition, DefaultDefinesEverything) {
    auto defs = Emit({});
    EXPECT_TRUE(Has(defs, "A"));
    EXPECT_TRUE(Has(defs, "B"));
}

TEST(CodegenPartition, RestrictToDefinitionsDefinesOnlyListed) {
    std::unordered_set<std::string> only{"A"};
    NCodeGen::TLLVMCodeGenOptions opts;
    opts.RestrictToDefinitions = &only;
    auto defs = Emit(opts);
    EXPECT_TRUE(Has(defs, "A"));
    EXPECT_FALSE(Has(defs, "B")); // B is an external declaration
}

TEST(CodegenPartition, EmitAsExternalDeclaresListed) {
    std::unordered_set<std::string> ext{"A"};
    NCodeGen::TLLVMCodeGenOptions opts;
    opts.EmitAsExternal = &ext;
    auto defs = Emit(opts);
    EXPECT_FALSE(Has(defs, "A")); // A is an external declaration
    EXPECT_TRUE(Has(defs, "B"));
}

int main(int argc, char** argv) {
    NQumir::NCodeGen::TLLVMInitializer llvmInit;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
