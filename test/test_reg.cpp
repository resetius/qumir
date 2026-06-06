#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <algorithm>

#include <qumir/parser/lexer.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
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
#include <qumir/modules/keyboard/keyboard.h>
#include <qumir/ir/lowering/lower_ast.h>
#include <qumir/ir/builder.h>

#include <qumir/runtime/runtime.h>

#include <qumir/codegen/llvm/llvm_initializer.h>
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

struct TModuleSet {
    NRegistry::SystemModule system;
    NRegistry::TurtleModule turtle;
    NRegistry::RobotModule robot;
    NRegistry::DrawerModule drawer;
    NRegistry::PainterModule painter;
    NRegistry::ComplexModule complex;
    NRegistry::ColorsModule colors;
    NRegistry::KeyboardModule keyboard;
};

void RegisterRuntimeModules(NSemantics::TNameResolver& resolver, TModuleSet& mods) {
    resolver.RegisterModule(&mods.system);
    resolver.RegisterModule(&mods.turtle);
    resolver.RegisterModule(&mods.robot);
    resolver.RegisterModule(&mods.drawer);
    resolver.RegisterModule(&mods.painter);
    resolver.RegisterModule(&mods.complex);
    resolver.RegisterModule(&mods.colors);
    resolver.RegisterModule(&mods.keyboard);
    resolver.ImportModule(mods.system.Name());
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

std::vector<ProgCase> Collect(const fs::path& root, std::string_view extension = ".kum") {
    std::vector<ProgCase> v;
    if (!fs::exists(root)) {
        return v;
    }

    auto casePath = [&](const fs::path& p) {
        auto rel = fs::relative(p, root);
        return rel.replace_extension();
    };

    for (auto& e : fs::recursive_directory_iterator(root)) {
        if (e.is_regular_file() && e.path().extension() == extension) {
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
    TModuleSet mods;
    NSemantics::TNameResolver nr;
    RegisterRuntimeModules(nr, mods);

    auto parsed = p.parse(ts, &nr);
    if (!parsed) {
        return parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, nr, {.EnableCoroutineAnalysis = true});
    if (!error) {
        return error.error().ToString() + "\n";
    }

    std::ostringstream out;
    out << expr;
    return out.str();
}

std::string BuildCoreSource(NAst::TTokenStream& ts) {
    NAst::TParser p;
    TModuleSet mods;
    NSemantics::TNameResolver nr;
    RegisterRuntimeModules(nr, mods);

    auto parsed = p.parse(ts, &nr);
    if (!parsed) {
        return parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    auto error = NTransform::Pipeline(expr, nr);
    if (!error) {
        return error.error().ToString() + "\n";
    }

    return NAst::NCore::PrintAst(expr, {.TypeMode = NAst::NCore::ETypePrintMode::All});
}

std::string BuildIR(std::istream& input, bool coreInput = false) {
    TModuleSet mods;
    NSemantics::TNameResolver resolver;
    RegisterRuntimeModules(resolver, mods);

    std::expected<NAst::TExprPtr, TError> parsed;
    if (coreInput) {
        NAst::NCore::TTokenStream ts(input);
        NAst::NCore::TParser p;
        parsed = p.Parse(ts);
        if (parsed) {
            resolver.ApplyPragmas(p.Pragmas);
        }
    } else {
        NAst::TTokenStream ts(input);
        NAst::TParser p;
        parsed = p.parse(ts, &resolver);
    }
    if (!parsed) {
        return parsed.error().ToString() + "\n";
    }

    auto expr = parsed.value();
    if (coreInput) {
        auto scope = resolver.GetOrCreateRootScope();
        // scope->AllowsRedeclare = true;
        scope->RootLevel = false;
        if (auto resolveError = resolver.Resolve(expr)) {
            return resolveError->ToString() + "\n";
        }
    }
    auto error = NTransform::Pipeline(expr, resolver, {.EnableCoroutineAnalysis = true});
    if (!error) {
        return error.error().ToString() + "\n";
    }

    NIR::TModule module;
    NIR::TBuilder builder(module);
    NIR::TAstLowerer lowerer(module, builder, resolver);
    auto lowerRes = lowerer.LowerTop(expr);
    if (!lowerRes) {
        return lowerRes.error().ToString();
    }

    std::ostringstream out;
    module.Print(out);
    return out.str();
}

} // namespace

class RegAst : public ::testing::TestWithParam<ProgCase> {};
class RegExec : public ::testing::TestWithParam<ProgCase> {};
class RegCoreLang : public ::testing::TestWithParam<ProgCase> {};

std::string ResultString(const std::expected<std::optional<std::string>, TError>& res) {
    if (!res) {
        return res.error().ToString();
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

enum class EExecBackend {
    IR,
    LLVM,
};

bool IsExecDisabled(const std::string& code) {
    auto header = code.substr(0, code.find('\n'));
    return header.find("disable_exec") != std::string::npos;
}

std::pair<std::string, std::string> RunExec(
    const std::string& code,
    const fs::path& stdin,
    bool coreInput,
    EExecBackend backend,
    int optLevel)
{
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

    std::expected<std::optional<std::string>, TError> res;
    if (backend == EExecBackend::IR) {
        TIRRunner runner(std::cout, std::cin, {
            .CoreInput = coreInput,
            .ResolveCoreInput = coreInput,
            .OptLevel = optLevel,
        });
        res = runner.Run(input);
    } else {
        TLLVMRunner runner({
            .CoreInput = coreInput,
            .ResolveCoreInput = coreInput,
            .OptLevel = optLevel,
        });
        res = runner.Run(input);
    }
    return {ResultString(res), out.str()};
}

void CheckExecCase(
    const fs::path& src,
    const fs::path& base,
    bool coreInput,
    EExecBackend backend,
    int optLevel,
    std::string_view label)
{
    const fs::path stdin = fs::path(src).replace_extension(".stdin");
    const fs::path golden = fs::path(GoldensDir / base).replace_extension(".result");
    const fs::path goldenStdOut = fs::path(GoldensDir / base).replace_extension(".result.stdout");

    const auto code = ReadAll(src);
    if (IsExecDisabled(code)) {
        GTEST_SKIP() << "Execution disabled for this test case";
    }

    auto [got, stdoutText] = RunExec(code, stdin, coreInput, backend, optLevel);
    CheckExecGoldens(src, golden, goldenStdOut, got, stdoutText, label);
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

    std::string got = BuildIR(input);

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
    CheckExecCase(src, GetParam().base, false, EExecBackend::IR, 0, "IR RUN");
}

TEST_P(RegExec, ExecIROPT) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    CheckExecCase(src, GetParam().base, false, EExecBackend::IR, 1, "IR OPT RUN");
}

TEST_P(RegExec, ExecLLVM) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    CheckExecCase(src, GetParam().base, false, EExecBackend::LLVM, 0, "LLVM RUN");
}

TEST_P(RegExec, ExecLLVMOPT) {
    const fs::path src = fs::path(CasesDir / GetParam().base).replace_extension(".kum");
    CheckExecCase(src, GetParam().base, false, EExecBackend::LLVM, 1, "LLVM OPT RUN");
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

    TIRRunner runner(std::cout, std::cin, {
        .CoreInput = true,
        .ResolveCoreInput = false,
    });
    auto res = runner.Run(coreInput);
    auto got = ResultString(res);

    CheckExecGoldens(src, golden, goldenStdOut, got, out.str(), "CORE RUN");
}

TEST_P(RegCoreLang, IR) {
    const fs::path relBase = fs::path("corelang") / GetParam().base;
    const fs::path src = fs::path(CasesDir / relBase).replace_extension(".oz");
    const fs::path golden = fs::path(GoldensDir / relBase).replace_extension(".ir");

    const auto code = ReadAll(src);
    std::istringstream input(code);

    std::string got = BuildIR(input, true);

    if (printOutput) {
        std::cout << "=== Output CORE IR for " << src << " ===\n";
        std::cout << got << "\n";
        std::cout << "=== End of output ===\n";
    }

    if (updateGoldens) {
        WriteAll(golden, got);
    }
    if (!fs::exists(golden)) {
        std::cerr << "Missing golden CORE IR file: " << golden << "\n";
        FAIL();
    }
    const auto exp = ReadAll(golden);
    EXPECT_EQ(got, exp);
}

TEST_P(RegCoreLang, ExecIR) {
    const fs::path relBase = fs::path("corelang") / GetParam().base;
    const fs::path src = fs::path(CasesDir / relBase).replace_extension(".oz");
    CheckExecCase(src, relBase, true, EExecBackend::IR, 0, "CORE IR RUN");
}

TEST_P(RegCoreLang, ExecIROPT) {
    const fs::path relBase = fs::path("corelang") / GetParam().base;
    const fs::path src = fs::path(CasesDir / relBase).replace_extension(".oz");
    CheckExecCase(src, relBase, true, EExecBackend::IR, 1, "CORE IR OPT RUN");
}

TEST_P(RegCoreLang, ExecLLVM) {
    const fs::path relBase = fs::path("corelang") / GetParam().base;
    const fs::path src = fs::path(CasesDir / relBase).replace_extension(".oz");
    CheckExecCase(src, relBase, true, EExecBackend::LLVM, 0, "CORE LLVM RUN");
}

TEST_P(RegCoreLang, ExecLLVMOPT) {
    const fs::path relBase = fs::path("corelang") / GetParam().base;
    const fs::path src = fs::path(CasesDir / relBase).replace_extension(".oz");
    CheckExecCase(src, relBase, true, EExecBackend::LLVM, 1, "CORE LLVM OPT RUN");
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

INSTANTIATE_TEST_SUITE_P(
    RegCoreLangProg,
    RegCoreLang,
    ::testing::ValuesIn(Collect(CasesDir / "corelang", ".oz")),
    [](const ::testing::TestParamInfo<ProgCase>& i){ return "CORE_" + NameFromPath(i.param.base); });

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
    NCodeGen::TLLVMInitializer llvmInit;
    return RUN_ALL_TESTS();
}
