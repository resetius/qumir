#include <gtest/gtest.h>

#include <qumir/runner/runner_ir.h>
#include <qumir/runtime/io.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace NQumir;
namespace fs = std::filesystem;

namespace {

class ModuleExecTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        Dir = fs::temp_directory_path() / ("qumir_modexec_" + std::to_string(++counter));
        fs::remove_all(Dir);
        fs::create_directories(Dir);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove_all(Dir, ec);
    }
    void WriteModule(const std::string& name, const std::string& content) {
        std::ofstream out(Dir / (name + ".oz"));
        out << content;
    }
    std::string Run(const std::string& main) {
        std::ostringstream out;
        NRuntime::SetOutputStream(&out);
        std::istringstream in;
        TIRRunner runner(out, in, TIRRunnerOptions{
            .CoreInput = true,
            .ResolveCoreInput = true,
            .Prelude = {"System"},
            .ModuleSearchPaths = {Dir.string()},
        });
        std::istringstream src(main);
        auto res = runner.Run(src);
        EXPECT_TRUE(res) << (res ? "" : res.error().ToString());
        return out.str();
    }
    fs::path Dir;
};

TEST_F(ModuleExecTest, MainCallsImportedFunction) {
    WriteModule("mod",
        "(block (fun add ((var a i64) (var b i64)) -> i64 (block (return (+ a b)))))");

    auto output = Run(
        "(block (use mod) (fun <main> () (block (output (call add (: 2 i64) (: 3 i64)) \"\\n\"))))");

    EXPECT_EQ(output, "5\n");
}

TEST_F(ModuleExecTest, TransitiveImport) {
    WriteModule("low", "(block (fun base () -> i64 (block (return (: 40 i64)))))");
    WriteModule("mid",
        "(block (use low) (fun bump () -> i64 (block (return (+ (call base) (: 2 i64))))))");

    auto output = Run(
        "(block (use mid) (fun <main> () (block (output (call bump) \"\\n\"))))");

    EXPECT_EQ(output, "42\n");
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
