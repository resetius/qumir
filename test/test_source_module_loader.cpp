#include <gtest/gtest.h>

#include <qumir/frontend/source_module_loader.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace NQumir;
using namespace NQumir::NFrontend;

namespace fs = std::filesystem;

namespace {

class SourceModuleLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        static int counter = 0;
        Dir = fs::temp_directory_path() / fs::path("qumir_loader_test_" + std::to_string(++counter));
        fs::remove_all(Dir);
        fs::create_directories(Dir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(Dir, ec);
    }

    fs::path Write(const std::string& name, const std::string& content) {
        auto path = Dir / (name + ".oz");
        std::ofstream out(path);
        out << content;
        out.close();
        return path;
    }

    bool DependencyFirst(const std::vector<const TSourceModule*>& order) {
        std::vector<std::string> seen;
        for (const auto* m : order) {
            for (const auto& dep : m->SourceDependencies) {
                if (std::find(seen.begin(), seen.end(), dep) == seen.end()) {
                    return false;
                }
            }
            seen.push_back(m->Name);
        }
        return true;
    }

    fs::path Dir;
};

TEST_F(SourceModuleLoaderTest, SingleModuleNoDeps) {
    Write("a", "(block (type t i64) (fun foo () (block)) (fun bar () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_TRUE(m) << m.error().ToString();
    EXPECT_EQ((*m)->Name, "a");
    EXPECT_TRUE((*m)->SourceDependencies.empty());
    EXPECT_EQ((*m)->ExportedFunctions(), (std::vector<std::string>{"foo", "bar"}));
    EXPECT_EQ((*m)->ExportedTypes(), (std::vector<std::string>{"t"}));
}

TEST_F(SourceModuleLoaderTest, TransitiveChain) {
    Write("a", "(block (use b) (fun fa () (block)))");
    Write("b", "(block (use c) (fun fb () (block)))");
    Write("c", "(block (fun fc () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_TRUE(m) << m.error().ToString();

    auto order = loader.TopologicalOrder();
    EXPECT_EQ(order.size(), 3u);
    EXPECT_TRUE(DependencyFirst(order));
    EXPECT_EQ(order.back()->Name, "a");
}

TEST_F(SourceModuleLoaderTest, DiamondDependency) {
    Write("a", "(block (use b) (use c) (fun fa () (block)))");
    Write("b", "(block (use d) (fun fb () (block)))");
    Write("c", "(block (use d) (fun fc () (block)))");
    Write("d", "(block (fun fd () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_TRUE(m) << m.error().ToString();

    auto order = loader.TopologicalOrder();
    EXPECT_EQ(order.size(), 4u); // d loaded once
    EXPECT_TRUE(DependencyFirst(order));
    EXPECT_EQ(order.back()->Name, "a");
}

TEST_F(SourceModuleLoaderTest, DirectCycle) {
    Write("a", "(block (use a) (fun fa () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_FALSE(m);
    EXPECT_NE(m.error().ToString().find("цикл"), std::string::npos);
}

TEST_F(SourceModuleLoaderTest, IndirectCycle) {
    Write("a", "(block (use b) (fun fa () (block)))");
    Write("b", "(block (use a) (fun fb () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_FALSE(m);
    auto msg = m.error().ToString();
    EXPECT_NE(msg.find("цикл"), std::string::npos);
    EXPECT_NE(msg.find("a -> b -> a"), std::string::npos) << msg;
}

TEST_F(SourceModuleLoaderTest, ModuleNotFound) {
    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("missing");
    ASSERT_FALSE(m);
    auto msg = m.error().ToString();
    EXPECT_NE(msg.find("не найден"), std::string::npos);
    EXPECT_NE(msg.find(Dir.string()), std::string::npos) << msg;
}

TEST_F(SourceModuleLoaderTest, DuplicateExplicitRegistration) {
    auto first = Write("a", "(block (fun fa () (block)))");
    auto other = Dir / "sub";
    fs::create_directories(other);
    std::ofstream out(other / "a.oz");
    out << "(block (fun fa () (block)))";
    out.close();

    TSourceModuleLoader loader;
    ASSERT_TRUE(loader.RegisterSourceModule(first));
    auto res = loader.RegisterSourceModule(other / "a.oz");
    ASSERT_FALSE(res);
    EXPECT_NE(res.error().ToString().find("уже зарегистрирован"), std::string::npos);
}

TEST_F(SourceModuleLoaderTest, IdempotentLoad) {
    Write("a", "(block (fun fa () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto first = loader.Load("a");
    auto second = loader.Load("a");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(*first, *second);
    EXPECT_EQ(loader.TopologicalOrder().size(), 1u);
}

TEST_F(SourceModuleLoaderTest, ForbidEntryPoint) {
    Write("a", "(block (fun <main> () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_FALSE(m);
    auto msg = m.error().ToString();
    EXPECT_NE(msg.find("<main>"), std::string::npos);
    EXPECT_NE(msg.find("a.oz"), std::string::npos) << msg; // diagnostic names the file
}

TEST_F(SourceModuleLoaderTest, GlobalExported) {
    Write("a", "(block (var g i64) (fun fa () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_TRUE(m) << m.error().ToString();
    EXPECT_EQ((*m)->ExportedGlobals(), (std::vector<std::string>{"g"}));
}

TEST_F(SourceModuleLoaderTest, ForbidExecutableTopLevel) {
    Write("a", "(block (output \"hi\") (fun fa () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_FALSE(m);
    EXPECT_NE(m.error().ToString().find("недопустимое верхнеуровневое"), std::string::npos);
}

TEST_F(SourceModuleLoaderTest, ExternalDependencyRecordedNotFollowed) {
    Write("a", "(block (use System) (fun fa () (block)))");

    TSourceModuleLoader loader;
    loader.AddSearchPath(Dir);

    auto m = loader.Load("a");
    ASSERT_TRUE(m) << m.error().ToString();
    EXPECT_EQ((*m)->Dependencies, (std::vector<std::string>{"System"}));
    EXPECT_TRUE((*m)->SourceDependencies.empty());
}

} // namespace

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
