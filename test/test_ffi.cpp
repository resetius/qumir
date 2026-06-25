#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <qumir/ir/ffi.h>

using namespace NQumir::NIR;
using namespace NQumir::NIR::NFFI;

namespace {

struct TPoint {
    int64_t x;
    int64_t y;
};

struct TDPoint {
    double x;
    double y;
};

struct TIntBox {
    int64_t x;
};

struct TDoubleBox {
    double x;
};

struct TThin {
    char c;
};

struct TFat {
    int64_t a;
    int64_t b;
    int64_t c;
};

int64_t add_i64(int64_t a, int64_t b) {
    return a + b;
}

int64_t g_flag = 0;

void set_flag(int64_t v) {
    g_flag = v;
}

int64_t point_sum(TPoint p) {
    return p.x + p.y;
}

int64_t point_sum_ptr(TPoint* p) {
    return p->x + p->y;
}

double dpoint_sum(TDPoint p) {
    return p.x + p.y;
}

int64_t int_box_get(TIntBox p) {
    return p.x;
}

double double_box_get(TDoubleBox p) {
    return p.x;
}

TIntBox int_box_make(int64_t v) {
    return TIntBox{v};
}

TDoubleBox double_box_make(double v) {
    return TDoubleBox{v};
}

TPoint point_double(TPoint p) {
    return TPoint{p.x * 2, p.y * 2};
}

int64_t thin_get(TThin t) {
    return t.c;
}

// Structs larger than two eightbytes travel by pointer.
int64_t fat_sum(TFat* p) {
    return p->a + p->b + p->c;
}

} // namespace

TEST(FFI, Basic) {
    auto* symbol = reinterpret_cast<void*>((double(*)(double))sin);
    auto func = std::unique_ptr<IFunction>(BuildFFI(symbol, EKind::F64, EStructKind::None, 0,
        {EKind::F64}, {EStructKind::None}));
    std::vector<uint64_t> args = {std::bit_cast<uint64_t>(M_PI * 0.5)};
    double ans = LoadArg<double>((*func)(args.data(), args.size()));
    EXPECT_DOUBLE_EQ(ans, 1.0);
}

TEST(FFI, TwoIntArgs) {
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(add_i64),
        EKind::I64, EStructKind::None, 0,
        {EKind::I64, EKind::I64}, {EStructKind::None, EStructKind::None}));
    std::vector<uint64_t> args = {20, 22};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 42);
}

TEST(FFI, VoidReturn) {
    g_flag = 0;
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(set_flag),
        EKind::Void, EStructKind::None, 0, {EKind::I64}, {EStructKind::None}));
    std::vector<uint64_t> args = {7};
    (*func)(args.data(), args.size());
    EXPECT_EQ(g_flag, 7);
}

TEST(FFI, StructArgInteger) {
    TPoint p{3, 4};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(point_sum),
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::IntInt}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 7);
}

TEST(FFI, StructArgPointer) {
    TPoint p{3, 4};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(point_sum_ptr),
        EKind::I64, EStructKind::None, 0, {EKind::Ptr}, {EStructKind::None}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 7);
}

TEST(FFI, StructArgSse) {
    TDPoint p{1.5, 2.25};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(dpoint_sum),
        EKind::F64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::SseSse}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), 3.75);
}

TEST(FFI, StructArgSingleInt) {
    TIntBox p{42};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(int_box_get),
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Int}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 42);
}

TEST(FFI, StructArgSingleSse) {
    TDoubleBox p{3.5};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(double_box_get),
        EKind::F64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Sse}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), 3.5);
}

TEST(FFI, StructArgThin) {
    // One byte fits a single INTEGER eightbyte; the upper bytes of the slot are
    // ignored by the callee, so back the value with a full eightbyte.
    union {
        TThin t;
        int64_t raw;
    } u{};
    u.raw = 0;
    u.t.c = 42;
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(thin_get),
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Int}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&u.t)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 42);
}

TEST(FFI, StructArgFat) {
    TFat p{1, 2, 3};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(fat_sum),
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Memory}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 6);
}

TEST(FFI, StructReturnByValue) {
    TPoint p{3, 4};
    TPoint out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(point_double),
        EKind::Struct, EStructKind::IntInt, 0, {EKind::Struct}, {EStructKind::IntInt}));
    // args[0] is the hidden result pointer, then the by-value struct argument.
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&out), reinterpret_cast<uint64_t>(&p)};
    (*func)(args.data(), args.size());
    EXPECT_EQ(out.x, 6);
    EXPECT_EQ(out.y, 8);
}

TEST(FFI, StructReturnSingleInt) {
    TIntBox out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(int_box_make),
        EKind::Struct, EStructKind::Int, 0, {EKind::I64}, {EStructKind::None}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&out), 42};
    (*func)(args.data(), args.size());
    EXPECT_EQ(out.x, 42);
}

TEST(FFI, StructReturnSingleSse) {
    TDoubleBox out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(double_box_make),
        EKind::Struct, EStructKind::Sse, 0, {EKind::F64}, {EStructKind::None}));
    std::vector<uint64_t> args = {
        reinterpret_cast<uint64_t>(&out),
        std::bit_cast<uint64_t>(3.5),
    };
    (*func)(args.data(), args.size());
    EXPECT_DOUBLE_EQ(out.x, 3.5);
}

TEST(FFI, StructReturnFatUnsupported) {
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(point_double),
        EKind::Struct, EStructKind::Memory, sizeof(TFat), {EKind::I64}, {EStructKind::None}));
    EXPECT_EQ(func, nullptr);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
