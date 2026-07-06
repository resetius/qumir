#include <gtest/gtest.h>

#include <qumir/codegen/llvm/llvm_initializer.h>
#include <qumir/parser/core/lexer.h>
#include <qumir/parser/core/parser.h>
#include <qumir/runner/runner_llvm.h>

#include <array>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

constexpr const char* WordHashSource = R"(
(block
  (fun word_hash ((var data <ptr u8>) (var size i64)) -> i64
    (block
      (var hash = (cast (: -3750763034362895579 i64) u64))
      (var words = (cast data <ptr u64>))
      (var index i64)
      (= index (: 0 i64))
      (while (! (< size (+ index (: 8 i64))))
        (block
          (var word = (index words (>> index (: 3 i64))))
          (= hash (^ hash word))
          (= hash (* hash (: 1099511628211 u64)))
          (= index (+ index (: 8 i64)))))
      (while (< index size)
        (block
          (= hash (^ hash (cast (index data index) u64)))
          (= hash (* hash (: 1099511628211 u64)))
          (= index (+ index (: 1 i64)))))
      (return (cast hash i64))))))";

} // namespace

TEST(KernelAst, LoadsU64FromPointerIndex) {
    std::istringstream input(WordHashSource);
    NQumir::NAst::NCore::TTokenStream tokens(input);
    NQumir::NAst::NCore::TParser parser;
    auto parsed = parser.Parse(tokens);
    ASSERT_TRUE(parsed) << parsed.error().ToString();

    NQumir::TLLVMRunner runner({
        .NativeCode = true,
        .CoreInput = true,
        .ResolveCoreInput = true,
        .AllowOverloads = true,
        .EnablePerfJitEventListener = true,
        .OptLevel = 3,
    });

    std::string error;
    void* entry = runner.CompileKernelAst(*parsed, "word_hash", &error);
    ASSERT_NE(entry, nullptr) << error;

    auto wordHash = reinterpret_cast<int64_t(*)(uint8_t*, int64_t)>(entry);
    std::array<uint8_t, 8> data = {
        208, 186, 208, 187, 209, 142, 209, 135,
    };

    EXPECT_EQ(wordHash(data.data(), static_cast<int64_t>(data.size())),
        INT64_C(5922767304251906895));
}

int main(int argc, char** argv) {
    NQumir::NCodeGen::TLLVMInitializer llvmInit;
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
