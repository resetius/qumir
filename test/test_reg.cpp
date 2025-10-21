#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <algorithm>

#include <qumir/parser/lexer.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/builder.h>

#include <qumir/runner/runner_llvm.h>
#include <qumir/runner/runner_ir.h>

using namespace NQumir;
namespace fs = std::filesystem;

namespace {

fs::path RootDir = "regtest";
fs::path CasesDir = RootDir / "cases";
fs::path GoldensDir = RootDir / "goldens";

bool updateGoldens = false;
bool printOutput = false;

std::string ReadAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

void WriteAll(const fs::path& p, const std::string& s) {
    std::cerr << "Updating golden file: " << p << "\n";
    std::cerr << "Written " << s.size() << " bytes\n";
    std::cerr << "Data:\n" << s << "\n";
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary); out << s;
}

struct ProgCase { fs::path base; }; // base without extension

std::vector<ProgCase> Collect(const fs::path& root) {
    std::vector<ProgCase> v;

    auto casePath = [&](const fs::path& p) {
        auto rel = fs::relative(p, root);
        return rel.replace_extension();
    };

    for (auto& e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().extension() == ".kum") {
            auto path = e.path();
            v.push_back({ casePath(path) });
        }
    }
    std::sort(v.begin(), v.end(),
        [](auto& a, auto& b){ return a.base.string() < b.base.string(); });
    return v;
}

std::string NameFromPath(const fs::path& p) {
    std::string s = p.string();
    for (auto& c : s) if (c == '/' || c == '\\' || c == '.') c = '_';
    return s;
}

// TODO: move to utils
std::string BuildAst(NAst::TTokenStream& ts) {
    NAst::TParser p;
    auto parsed = p.parse(ts);
    if (!parsed) {
        return "Error: " + parsed.error().ToString() + "\n";
    }
    NSemantics::TNameResolver nr;
    NRegistry::SystemModule().Register(nr);

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, nr);
    if (!error) {
        return "Error: " + error.error().ToString() + "\n";
    }

    std::ostringstream out;
    out << *expr;
    return out.str();
}

// TODO: move to utils
std::string BuildIR(NAst::TTokenStream& ts) {
    NSemantics::TNameResolver resolver;
    NRegistry::SystemModule().Register(resolver);

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

    NIR::TModule module;
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

class RegAst : public ::testing::TestWithParam<ProgCase> {};
class RegExec : public ::testing::TestWithParam<ProgCase> {};

TEST_P(RegAst, Ast) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".ast");

    const auto code = ReadAll(src);
    std::istringstream input(code);

    NAst::TTokenStream ts(input);
    std::string got = BuildAst(ts);

    if (printOutput) {
        std::cout << "=== Output AST for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        // fail if golden missing
        std::cerr << "Missing golden AST file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

TEST_P(RegAst, IR) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".ir");

    const auto code = ReadAll(src);
    std::istringstream input(code);

    NAst::TTokenStream ts(input);
    std::string got = BuildIR(ts);

    if (printOutput) {
        std::cout << "=== Output IR for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        // fail if golden missing
        std::cerr << "Missing golden IR file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

TEST_P(RegExec, ExecIR) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    TIRRunner runner(std::cout, std::cin, {});
    auto res = runner.Run(input);
    std::string got;
    if (!res) {
        got = "Error: " + res.error().ToString() + "\n";
    } else {
        std::ostringstream out;
        out << *(res.value());
        got = out.str();
    }

    if (printOutput) {
        std::cout << "=== Output IR RUN for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        // fail if golden missing
        std::cerr << "Missing golden IR RUN file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

TEST_P(RegExec, ExecIROPT) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    TIRRunner runner(std::cout, std::cin, {.OptLevel = 1});
    auto res = runner.Run(input);
    std::string got;
    if (!res) {
        got = "Error: " + res.error().ToString() + "\n";
    } else {
        std::ostringstream out;
        out << *(res.value());
        got = out.str();
    }

    if (printOutput) {
        std::cout << "=== Output IR RUN for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        // fail if golden missing
        std::cerr << "Missing golden IR RUN file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

TEST_P(RegExec, ExecLLVM) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    TLLVMRunner runner;
    auto res = runner.Run(input);
    std::string got;
    if (!res) {
        got = "Error: " + res.error().ToString() + "\n";
    } else {
        std::ostringstream out;
        out << *(res.value());
        got = out.str();
    }

    if (printOutput) {
        std::cout << "=== Output LLVM RUN for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        // fail if golden missing
        std::cerr << "Missing golden LLVM RUN file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

INSTANTIATE_TEST_SUITE_P(
    RegProgAst,
    RegAst,
    ::testing::ValuesIn(Collect(CasesDir)),
    [](const ::testing::TestParamInfo<ProgCase>& i){ return "AST_" + NameFromPath(i.param.base); });

INSTANTIATE_TEST_SUITE_P(
    RegProgExec,
    RegExec,
    ::testing::ValuesIn(Collect(CasesDir)),
    [](const ::testing::TestParamInfo<ProgCase>& i){ return "EXEC_" + NameFromPath(i.param.base); });

int main(int argc, char** argv) {
    if (argc > 1) {
        RootDir = argv[1];
        CasesDir = RootDir / "cases";
        GoldensDir = RootDir / "goldens";
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--canonize")) {
            updateGoldens = true;
        } else if (!strcmp(argv[i], "--print")) {
            printOutput = true;
        }
    }
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
