#include <gtest/gtest.h>

#include <qumir/codegen/llvm/symbol_object_cache.h>

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace NQumir::NCodeGen;
namespace fs = std::filesystem;

namespace {

TBuildFingerprint Fp(std::string schema = "v1") {
    return TBuildFingerprint{
        .CacheSchema = std::move(schema),
        .KernelLibVersion = "k1",
        .LlvmVersion = "20",
        .Triple = "arm64-apple",
        .DataLayout = "dl",
        .CpuFeatures = "",
        .OptSettings = "O3",
    };
}

struct TCacheDir {
    fs::path Dir = fs::temp_directory_path() / fs::path("qdbcache-" + std::to_string(::getpid()) + "-" + std::to_string(rand()));
    TCacheDir() { fs::create_directories(Dir); }
    ~TCacheDir() { std::error_code ec; fs::remove_all(Dir, ec); }
    std::string Str() const { return Dir.string(); }
};

ERegisterResult Reg(TSymbolObjectCache& c, std::string_view bytes, std::vector<std::string> syms) {
    auto r = c.Register(bytes, syms);
    EXPECT_TRUE(r) << (r ? "" : r.error().what());
    return r.value_or(ERegisterResult::AlreadyPresent);
}

} // namespace

TEST(SymbolObjectCache, RegisterThenResolveHitAndMiss) {
    TCacheDir dir;
    auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(cache);

    EXPECT_EQ(Reg(*cache, "OBJ", {"A", "B"}), ERegisterResult::Installed);

    auto plan = cache->Resolve({"A", "B", "C"});
    EXPECT_EQ(plan.ObjectFiles.size(), 1u); // A and B share one object
    EXPECT_EQ(plan.Misses, std::vector<std::string>{"C"});
}

TEST(SymbolObjectCache, OverlapIsDiscardedFirstWriterWins) {
    TCacheDir dir;
    auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(cache);

    EXPECT_EQ(Reg(*cache, "OBJ1", {"A", "B"}), ERegisterResult::Installed);
    // B overlaps -> all-or-nothing discard, A/B stay on OBJ1, D not added.
    EXPECT_EQ(Reg(*cache, "OBJ2", {"B", "D"}), ERegisterResult::AlreadyPresent);

    auto plan = cache->Resolve({"A", "B", "D"});
    EXPECT_EQ(plan.ObjectFiles.size(), 1u);
    EXPECT_EQ(plan.Misses, std::vector<std::string>{"D"});
}

TEST(SymbolObjectCache, DistinctObjectsResolveTogether) {
    TCacheDir dir;
    auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(cache);

    EXPECT_EQ(Reg(*cache, "OBJ1", {"A"}), ERegisterResult::Installed);
    EXPECT_EQ(Reg(*cache, "OBJ2", {"B"}), ERegisterResult::Installed);

    auto plan = cache->Resolve({"A", "B"});
    EXPECT_EQ(plan.ObjectFiles.size(), 2u);
    EXPECT_TRUE(plan.Misses.empty());
}

TEST(SymbolObjectCache, PersistsAcrossReopen) {
    TCacheDir dir;
    {
        auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
        ASSERT_TRUE(cache);
        ASSERT_EQ(Reg(*cache, "OBJ", {"A"}), ERegisterResult::Installed);
    }
    auto reopened = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(reopened);
    auto plan = reopened->Resolve({"A"});
    EXPECT_EQ(plan.ObjectFiles.size(), 1u);
    EXPECT_TRUE(plan.Misses.empty());
}

TEST(SymbolObjectCache, SeparateGenerationsCoexist) {
    TCacheDir root;
    {
        auto v1 = TSymbolObjectCache::Open(root.Str(), Fp("v1"));
        ASSERT_TRUE(v1);
        ASSERT_EQ(Reg(*v1, "OBJ", {"A"}), ERegisterResult::Installed);
    }
    // Different fingerprint = different generation subdir; v1's A stays intact.
    auto v2 = TSymbolObjectCache::Open(root.Str(), Fp("v2"));
    ASSERT_TRUE(v2);
    EXPECT_EQ(v2->Resolve({"A"}).Misses, std::vector<std::string>{"A"});

    auto v1again = TSymbolObjectCache::Open(root.Str(), Fp("v1"));
    ASSERT_TRUE(v1again);
    EXPECT_TRUE(v1again->Resolve({"A"}).Misses.empty());
}

TEST(SymbolObjectCache, SeparatesNativeAndWasmByTriple) {
    TCacheDir root;
    auto native = Fp();
    native.Triple = "arm64-apple-macosx";
    auto wasm = Fp();
    wasm.Triple = "wasm32-unknown-unknown";

    auto n = TSymbolObjectCache::Open(root.Str(), native);
    ASSERT_TRUE(n);
    ASSERT_EQ(Reg(*n, "NATIVE", {"A"}), ERegisterResult::Installed);

    auto w = TSymbolObjectCache::Open(root.Str(), wasm);
    ASSERT_TRUE(w);
    EXPECT_EQ(w->Resolve({"A"}).Misses, std::vector<std::string>{"A"}); // isolated from native
}

TEST(SymbolObjectCache, SelfHealsAfterPartialOverlap) {
    TCacheDir dir;
    auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(cache);

    ASSERT_EQ(Reg(*cache, "OBJ1", {"A", "B"}), ERegisterResult::Installed);
    ASSERT_EQ(Reg(*cache, "OBJ2", {"B", "C"}), ERegisterResult::AlreadyPresent);
    // C was dropped with the overlap; registering it alone succeeds next time.
    ASSERT_EQ(Reg(*cache, "OBJ3", {"C"}), ERegisterResult::Installed);

    auto plan = cache->Resolve({"A", "B", "C"});
    EXPECT_EQ(plan.ObjectFiles.size(), 2u); // {A,B} and {C}
    EXPECT_TRUE(plan.Misses.empty());
}

TEST(SymbolObjectCache, ResolveDedupsRepeatedMisses) {
    TCacheDir dir;
    auto cache = TSymbolObjectCache::Open(dir.Str(), Fp());
    ASSERT_TRUE(cache);
    EXPECT_EQ(cache->Resolve({"A", "A"}).Misses, std::vector<std::string>{"A"});
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
