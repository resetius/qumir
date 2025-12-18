#include <gtest/gtest.h>

#include <qumir/parser/parser.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/passes/analysis/cfg.h>
#include <qumir/ir/passes/transforms/locals2ssa.h>
#include <qumir/ir/passes/transforms/de_ssa.h>
#include <qumir/ir/passes/transforms/renumber_regs.h>

#include <sstream>

using namespace NQumir;
using namespace NQumir::NIR;
using namespace NQumir::NIR::NPasses;

namespace {

// TODO: move to utils
std::string BuildIR(NAst::TTokenStream& ts, NIR::TModule& module) {
    NSemantics::TNameResolver resolver;
    NRegistry::SystemModule sys;
    resolver.RegisterModule(&sys);
    resolver.ImportModule(sys.Name());

    NAst::TParser p;
    auto parsed = p.parse(ts);
    if (!parsed) {
        return "Error: " + parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, resolver);
    if (!error) {
        return "Error: " + error.error().ToString() + "\n";
    }

    NIR::TBuilder builder(module);
    NIR::TAstLowerer lowerer(module, builder, resolver);
    auto lowerRes = lowerer.LowerTop(expr);
    if (!lowerRes) {
        return "Error: " + lowerRes.error().ToString() + "\n";
    }

    std::ostringstream out;
    module.Print(out);
    return out.str();
}

} // namespace

TEST(CfgTest, Basic) {
    std::string s = R"(
алг
нач
    цел ф
    ф := 0
    нц пока ф < 10
        ф := ф + 1
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);
    auto& function = module.Functions[0];
    BuildCfg(function);
    function.Print(std::cout, module);
    EXPECT_EQ(function.Blocks.size(), 5);
    EXPECT_EQ(function.Blocks[0].Succ.size(), 1);
    EXPECT_EQ(function.Blocks[1].Succ.size(), 2);
    EXPECT_EQ(function.Blocks[2].Succ.size(), 1);
    EXPECT_EQ(function.Blocks[3].Succ.size(), 1);

    EXPECT_EQ(function.Blocks[0].Pred.size(), 0);
    EXPECT_EQ(function.Blocks[1].Pred.size(), 2);
    EXPECT_EQ(function.Blocks[2].Pred.size(), 1);
    EXPECT_EQ(function.Blocks[3].Pred.size(), 1);

    EXPECT_EQ(function.Blocks[0].Succ, std::list<TLabel>{function.Blocks[1].Label});
    EXPECT_EQ(function.Blocks[1].Succ, (std::list<TLabel>{function.Blocks[2].Label, function.Blocks[3].Label}));
    EXPECT_EQ(function.Blocks[2].Succ, std::list<TLabel>{function.Blocks[1].Label});

    EXPECT_EQ(function.Blocks[1].Pred, (std::list<TLabel>{function.Blocks[0].Label, function.Blocks[2].Label}));
    EXPECT_EQ(function.Blocks[2].Pred, std::list<TLabel>{function.Blocks[1].Label});
    EXPECT_EQ(function.Blocks[3].Pred, std::list<TLabel>{function.Blocks[1].Label});
}

TEST(CfgTest, RPO) {
std::string s = R"(
алг
нач
    цел ф
    ф := 0
    нц пока ф < 10
        ф := ф + 1
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);

    BuildCfg(module.Functions[0]);
    std::vector<TLabel> rpo = ComputeRPO(module.Functions[0]);
    EXPECT_EQ(rpo, (std::vector<TLabel>{{0}, {2}, {4}, {1}, {3}}));
}

TEST(CfgTest, PromoteLocalsToSSAWhile) {
std::string s = R"(
алг
нач
    цел ф
    ф := 0
    нц пока ф < 10
        ф := ф + 1
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);

    auto& function = module.Functions[0];

    function.Print(std::cout, module);

    PromoteLocalsToSSA(function, module);

    function.Print(std::cout, module);
}

TEST(CfgTest, PromoteLocalsToSSAFor) {
std::string s = R"(
алг
нач
    цел ф, i
    ф := 0
    нц для i от 0 до 10 шаг 1
        ф := ф + 1
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);

    auto& function = module.Functions[0];

    function.Print(std::cout, module);

    PromoteLocalsToSSA(function, module);

    function.Print(std::cout, module);
}

TEST(CfgTest, PromoteLocalsToSSAFactorial) {
std::string s = R"(
алг цел факториал(цел число)
нач
    цел i
    знач := 1
    нц для i от 1 до число
        знач := знач * i
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);

    auto& function = module.Functions[0];

    function.Print(std::cout, module);

    PromoteLocalsToSSA(function, module);

    function.Print(std::cout, module);

    DeSSA(function, module);

    function.Print(std::cout, module);

    RenumberRegisters(function, module);

    function.Print(std::cout, module);
}

TEST(CfgTest, PromoteLocalsToSSAContinue) {
std::string s = R"(
алг
нач
    цел i, s
    i := 0
    s := 0
    нц пока i < 4
        если mod(i, 2) = 0 то
            i := i + 1
            далее
        все
        s := s + i
        i := i + 1
    кц
кон
    )";

    std::istringstream ss(s);
    NAst::TTokenStream ts(ss);
    NIR::TModule module;
    std::string got = BuildIR(ts, module);
    ASSERT_TRUE(module.Functions.size() == 1);

    auto& function = module.Functions[0];

    function.Print(std::cout, module);

    PromoteLocalsToSSA(function, module);

    function.Print(std::cout, module);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
