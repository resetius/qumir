#include <gtest/gtest.h>

#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/runner/runner_llvm.h>

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>

using namespace NQumir;
namespace fs = std::filesystem;

namespace {

struct TCacheDir {
    fs::path Dir = fs::temp_directory_path() / fs::path("qdbcompile-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
    TCacheDir() { fs::create_directories(Dir); }
    ~TCacheDir() { std::error_code ec; fs::remove_all(Dir, ec); }
    std::string Str() const { return Dir.string(); }
};

int CountObjects(const fs::path& dir) {
    int n = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
        if (it->path().extension() == ".o") {
            ++n;
        }
    }
    return n;
}

// dep() = 40 is a cacheable dependency; kernel() = dep() + 2 links against it.
constexpr const char* Source =
    "(block"
    "  (fun dep () -> i64 (attrs cacheable) (block (return (: 40 i64))))"
    "  (fun kernel () -> i64 (block (return (+ (call dep) (: 2 i64))))))";

NCodeGen::TLlvmRunner::TLinkedModule Compile(
    const std::string& cacheDir, const char* source, const std::string& entry, std::string* err)
{
    std::istringstream in(source);
    NAst::NCore::TTokenStream tokens(in);
    NAst::NCore::TParser parser;
    auto parsed = parser.Parse(tokens);
    EXPECT_TRUE(parsed) << (parsed ? "" : parsed.error().ToString());
    if (!parsed) {
        return {};
    }
    TLLVMRunner runner({
        .NativeCode = true,
        .CoreInput = true,
        .ResolveCoreInput = true,
        .AllowOverloads = true,
        .OptLevel = 0,
    });
    return runner.CompileFusedKernelsCached(*parsed, {entry}, cacheDir, "v1", "k1", err);
}

} // namespace

TEST(CachedCompile, MissCompilesAndPersists) {
    TCacheDir cache;
    std::string err;
    auto linked = Compile(cache.Str(), Source, "kernel", &err);
    ASSERT_FALSE(linked.Entries.empty()) << err;

    auto* kernel = reinterpret_cast<int64_t (*)()>(linked.Entries["kernel"]);
    EXPECT_EQ(kernel(), 42); // dep() + 2, dep linked from a separate object
    EXPECT_EQ(CountObjects(cache.Dir), 1); // the dependency object was persisted
}

TEST(CachedCompile, HitReusesObjectAcrossRunners) {
    TCacheDir cache;
    std::string err;

    auto first = Compile(cache.Str(), Source, "kernel", &err);
    ASSERT_FALSE(first.Entries.empty()) << err;
    ASSERT_EQ(CountObjects(cache.Dir), 1);

    // Fresh runner, same cache dir: dep is a hit, no new object is compiled.
    auto second = Compile(cache.Str(), Source, "kernel", &err);
    ASSERT_FALSE(second.Entries.empty()) << err;
    auto* kernel = reinterpret_cast<int64_t (*)()>(second.Entries["kernel"]);
    EXPECT_EQ(kernel(), 42);
    EXPECT_EQ(CountObjects(cache.Dir), 1); // still one object: cache hit
}

// A cacheable function that itself calls a cacheable function, where the callee
// is a cache hit and the caller a miss: the miss object references the hit
// object across the split. Works because each compile re-derives the full
// (monomorphized) cacheable set, so Resolve pulls both objects.
TEST(CachedCompile, TransitiveCacheableAcrossObjects) {
    TCacheDir cache;
    std::string err;

    // Run 1: only B, so B lands in its own cached object.
    constexpr const char* onlyB =
        "(block"
        "  (fun B () -> i64 (attrs cacheable) (block (return (: 40 i64))))"
        "  (fun kb () -> i64 (block (return (call B)))))";
    auto b = Compile(cache.Str(), onlyB, "kb", &err);
    ASSERT_FALSE(b.Entries.empty()) << err;
    const int afterB = CountObjects(cache.Dir);
    ASSERT_EQ(afterB, 1);

    // Run 2: A calls B; B is a hit, A a miss -> A's object references B's object.
    constexpr const char* aCallsB =
        "(block"
        "  (fun B () -> i64 (attrs cacheable) (block (return (: 40 i64))))"
        "  (fun A () -> i64 (attrs cacheable) (block (return (call B))))"
        "  (fun kernel () -> i64 (block (return (+ (call A) (: 2 i64))))))";
    auto k = Compile(cache.Str(), aCallsB, "kernel", &err);
    ASSERT_FALSE(k.Entries.empty()) << err; // empty would mean the cross-object link failed
    auto* kernel = reinterpret_cast<int64_t (*)()>(k.Entries["kernel"]);
    EXPECT_EQ(kernel(), 42); // A() = B() = 40, + 2
    EXPECT_EQ(CountObjects(cache.Dir), 2); // B.o reused, A.o added
}

int main(int argc, char** argv) {
    NQumir::NCodeGen::TLLVMInitializer llvmInit;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
