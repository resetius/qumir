#include <gtest/gtest.h>

#include <cmath>
#include <qumir/ir/ffi.h>

using namespace NQumir::NIR;
using namespace NQumir::NIR::NFFI;

TEST(FFI, Basic) {
    auto* symbol = reinterpret_cast<void*>((double(*)(double))sin);
    auto func = std::unique_ptr<IFunction>(BuildFFI(symbol, EKind::F64, /*unused=*/0, /*kinds*/{EKind::F64}, /*sizes*/{}));
    std::vector<uint64_t> args = {std::bit_cast<uint64_t>(M_PI*0.5)};
    double ans = LoadArg<double>((*func)(args.data(), args.size()));
    EXPECT_DOUBLE_EQ(ans, 1.0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
