#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <algorithm>

#include <qumir/parser/lexer.h>
#include <qumir/parser/core/printer.h>
#include <qumir/parser/parser.h>
#include <qumir/semantics/name_resolution/name_resolver.h>
#include <qumir/semantics/type_annotation/type_annotation.h>
#include <qumir/semantics/transform/transform.h>
#include <qumir/modules/system/system.h>
#include <qumir/modules/turtle/turtle.h>
#include <qumir/modules/robot/robot.h>
#include <qumir/modules/drawer/drawer.h>
#include <qumir/modules/painter/painter.h>
#include <qumir/modules/complex/complex.h>
#include <qumir/modules/colors/colors.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/builder.h>

#include <qumir/runtime/runtime.h>

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

void RegisterRuntimeModules(NSemantics::TNameResolver& resolver) {
    static NRegistry::SystemModule system;
    static NRegistry::TurtleModule turtle;
    static NRegistry::RobotModule robot;
    static NRegistry::DrawerModule drawer;
    static NRegistry::PainterModule painter;
    static NRegistry::ComplexModule complex;
    static NRegistry::ColorsModule colors;

    resolver.RegisterModule(&system);
    resolver.RegisterModule(&turtle);
    resolver.RegisterModule(&robot);
    resolver.RegisterModule(&drawer);
    resolver.RegisterModule(&painter);
    resolver.RegisterModule(&complex);
    resolver.RegisterModule(&colors);
    resolver.ImportModule(system.Name());
}

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
    NSemantics::TNameResolver nr;
    RegisterRuntimeModules(nr);

    auto parsed = p.parse(ts, &nr);
    if (!parsed) {
        return "Error: " + parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, nr, {.EnableCoroutineAnalysis = true});
    if (!error) {
        return "Error: " + error.error().ToString() + "\n";
    }

    std::ostringstream out;
    out << expr;
    return out.str();
}

std::string BuildCoreSource(NAst::TTokenStream& ts) {
    NAst::TParser p;
    NSemantics::TNameResolver nr;
    RegisterRuntimeModules(nr);

    auto parsed = p.parse(ts, &nr);
    if (!parsed) {
        return "Error: " + parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, nr);
    if (!error) {
        return "Error: " + error.error().ToString() + "\n";
    }

    return NAst::NCore::PrintAst(expr, {.TypeMode = NAst::NCore::ETypePrintMode::All});
}

// TODO: move to utils
std::string BuildIR(NAst::TTokenStream& ts) {
    NSemantics::TNameResolver resolver;
    RegisterRuntimeModules(resolver);

    NAst::TParser p;
    auto parsed = p.parse(ts, &resolver);
    if (!parsed) {
        return "Error: " + parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, resolver, {.EnableCoroutineAnalysis = true});
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

std::string ResultString(const std::expected<std::optional<std::string>, TError>& res) {
    if (!res) {
        return "Error: " + res.error().ToString() + "\n";
    }
    std::ostringstream out;
    if (res.value().has_value()) {
        out << *(res.value());
    }
    return out.str();
}

void CheckExecGoldens(
    const fs::path& src,
    const fs::path& golden,
    const fs::path& goldenStdOut,
    const std::string& got,
    const std::string& stdoutText,
    std::string_view label)
{
    if (printOutput) {
        std::cout << "=== Output " << label << " for " << src << " ===\n";
        std::cout << "Result: " << got << "\n";
        std::cout << "StdOut: " << stdoutText << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
        if (!stdoutText.empty()) {
            WriteAll(goldenStdOut, stdoutText);
        }
    }
    if (!fs::exists(golden)) {
        std::cerr << "Missing golden " << label << " file: " << golden << "\n";
        FAIL();
    }
    if (!fs::exists(goldenStdOut) && !stdoutText.empty()) {
        std::cerr << "Missing golden " << label << " stdout file: " << goldenStdOut << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
    if (fs::exists(goldenStdOut)) {
        const auto expOut = ReadAll(goldenStdOut);
        EXPECT_EQ(stdoutText, expOut);
    }
}

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
    const fs::path stdin = fs::path(CasesDir / GetParam().base).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / GetParam().base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istream* inStream = nullptr;
    std::ifstream fin;
    if (fs::exists(stdin)) {
        fin.open(stdin, std::ios::binary);
        inStream = &fin;
    }
    NRuntime::SetInputStream(inStream);

    TIRRunner runner(std::cout, std::cin, {});
    auto res = runner.Run(input);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "IR RUN");
}

TEST_P(RegExec, ExecIROPT) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path stdin = fs::path(CasesDir / GetParam().base).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / GetParam().base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istream* inStream = nullptr;
    std::ifstream fin;
    if (fs::exists(stdin)) {
        fin.open(stdin, std::ios::binary);
        inStream = &fin;
    }
    NRuntime::SetInputStream(inStream);

    TIRRunner runner(std::cout, std::cin, {.OptLevel = 1});
    auto res = runner.Run(input);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "IR OPT RUN");
}

TEST_P(RegExec, ExecLLVM) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path stdin = fs::path(CasesDir / GetParam().base).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / GetParam().base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istream* inStream = nullptr;
    std::ifstream fin;
    if (fs::exists(stdin)) {
        fin.open(stdin, std::ios::binary);
        inStream = &fin;
    }
    NRuntime::SetInputStream(inStream);

    TLLVMRunner runner;
    auto res = runner.Run(input);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "LLVM RUN");
}

TEST_P(RegExec, ExecLLVMOPT) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path stdin = fs::path(CasesDir / GetParam().base).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / GetParam().base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }
    std::istringstream input(code);

    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istream* inStream = nullptr;
    std::ifstream fin;
    if (fs::exists(stdin)) {
        fin.open(stdin, std::ios::binary);
        inStream = &fin;
    }
    NRuntime::SetInputStream(inStream);

    TLLVMRunner runner({.OptLevel = 1});
    auto res = runner.Run(input);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "LLVM OPT RUN");
}

TEST_P(RegExec, CoreExec) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    const fs::path stdin = fs::path(CasesDir / GetParam().base).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / GetParam().base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / GetParam().base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    auto header = code.substr(0, code.find('\n'));
    if (header.find("disable_exec") != std::string::npos) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }

    std::istringstream kumirInput(code);
    NAst::TTokenStream ts(kumirInput);
    auto coreSource = BuildCoreSource(ts);
    if (coreSource.starts_with("Error: ")) {
        CheckExecGoldens(src, golden, goldenStdOut, coreSource, "", "CORE RUN");
        return;
    }

    std::istringstream coreInput(coreSource);
    std::ostringstream out;
    NRuntime::SetOutputStream(&out);
    std::istream* inStream = nullptr;
    std::ifstream fin;
    if (fs::exists(stdin)) {
        fin.open(stdin, std::ios::binary);
        inStream = &fin;
    }
    NRuntime::SetInputStream(inStream);

    TIRRunner runner(std::cout, std::cin, {.CoreInput = true});
    auto res = runner.Run(coreInput);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "CORE RUN");
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
