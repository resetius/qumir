#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>
#include <qumir/ir/ffi.h>

using namespace NQumir::NIR;
using namespace NQumir::NIR::NFFI;

// BuildFFI assumes the standard C ABI, so the called symbols must have external
// C linkage; a static/anonymous-namespace helper may get a non-standard calling
// convention under optimization.
extern "C" {

struct TPoint {
    int64_t x;
    int64_t y;
};

struct TDPoint {
    double x;
    double y;
};

struct TIntSse {
    int64_t a;
    double b;
};

struct TSseInt {
    double a;
    int64_t b;
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

struct TTwoChar {
    char a;
    char b;
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

int64_t two_char_sum(TTwoChar p) {
    return p.a + p.b;
}

TTwoChar two_char_make(int64_t a, int64_t b) {
    return TTwoChar{static_cast<char>(a), static_cast<char>(b)};
}

double int_sse_sum(TIntSse p) {
    return p.a + p.b;
}

double sse_int_sum(TSseInt p) {
    return p.a + p.b;
}

TIntSse int_sse_make(int64_t a, double b) {
    return TIntSse{a, b};
}

TSseInt sse_int_make(double a, int64_t b) {
    return TSseInt{a, b};
}

// A by-value struct larger than two eightbytes (Memory class).
int64_t fat_sum(TFat p) {
    return p.a + p.b + p.c;
}

// Returns a struct larger than two eightbytes by value (sret).
TFat fat_make(int64_t a, int64_t b, int64_t c) {
    return TFat{a * 2, b * 2, c * 2};
}

int64_t sum_10i(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                int64_t f, int64_t g, int64_t h, int64_t i, int64_t j) {
    return a + b + c + d + e + f + g + h + i + j;
}

double sum_10d(double a, double b, double c, double d, double e,
               double f, double g, double h, double i, double j) {
    return a + b + c + d + e + f + g + h + i + j;
}

double many_mixed(int64_t a, int64_t b, int64_t c, int64_t d, int64_t e,
                  int64_t f, int64_t g, int64_t h, int64_t i,
                  double p, double q, double r, double s, double t,
                  double u, double v, double w, double x) {
    return static_cast<double>(a + b + c + d + e + f + g + h + i)
        + (p + q + r + s + t + u + v + w + x);
}

// A by-value struct argument arriving when the GP file is nearly full forces
// the whole struct onto the stack on AArch64 (all-or-nothing register use).
int64_t seven_then_point(int64_t a, int64_t b, int64_t c, int64_t d,
                         int64_t e, int64_t f, int64_t g, TPoint p) {
    return a + b + c + d + e + f + g + p.x + p.y;
}

double seven_d_then_dpoint(double a, double b, double c, double d,
                           double e, double f, double g, TDPoint p) {
    return a + b + c + d + e + f + g + p.x + p.y;
}

} // extern "C"

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
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Memory}, {sizeof(TFat)}));
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

TEST(FFI, StructArgIntSse) {
    TIntSse p{3, 1.5};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(int_sse_sum),
        EKind::F64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::IntSse}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), 4.5);
}

TEST(FFI, StructArgSseInt) {
    TSseInt p{1.5, 3};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(sse_int_sum),
        EKind::F64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::SseInt}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&p)};
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), 4.5);
}

TEST(FFI, StructReturnIntSse) {
    TIntSse out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(int_sse_make),
        EKind::Struct, EStructKind::IntSse, 0,
        {EKind::I64, EKind::F64}, {EStructKind::None, EStructKind::None}));
    std::vector<uint64_t> args = {
        reinterpret_cast<uint64_t>(&out),
        7,
        std::bit_cast<uint64_t>(2.5),
    };
    (*func)(args.data(), args.size());
    EXPECT_EQ(out.a, 7);
    EXPECT_DOUBLE_EQ(out.b, 2.5);
}

TEST(FFI, StructReturnSseInt) {
    TSseInt out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(sse_int_make),
        EKind::Struct, EStructKind::SseInt, 0,
        {EKind::F64, EKind::I64}, {EStructKind::None, EStructKind::None}));
    std::vector<uint64_t> args = {
        reinterpret_cast<uint64_t>(&out),
        std::bit_cast<uint64_t>(2.5),
        7,
    };
    (*func)(args.data(), args.size());
    EXPECT_DOUBLE_EQ(out.a, 2.5);
    EXPECT_EQ(out.b, 7);
}

TEST(FFI, StructReturnMemory) {
    TFat out{};
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(fat_make),
        EKind::Struct, EStructKind::Memory, sizeof(TFat),
        {EKind::I64, EKind::I64, EKind::I64},
        {EStructKind::None, EStructKind::None, EStructKind::None}));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&out), 1, 2, 3};
    (*func)(args.data(), args.size());
    EXPECT_EQ(out.a, 2);
    EXPECT_EQ(out.b, 4);
    EXPECT_EQ(out.c, 6);
}

TEST(FFI, ManyIntArgsStackSpill) {
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(sum_10i),
        EKind::I64, EStructKind::None, 0,
        std::vector<EKind>(10, EKind::I64),
        std::vector<EStructKind>(10, EStructKind::None)));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 55);
}

TEST(FFI, ManyDoubleArgsStackSpill) {
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(sum_10d),
        EKind::F64, EStructKind::None, 0,
        std::vector<EKind>(10, EKind::F64),
        std::vector<EStructKind>(10, EStructKind::None)));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args;
    double expected = 0;
    for (int n = 1; n <= 10; ++n) {
        double v = n + 0.5;
        expected += v;
        args.push_back(std::bit_cast<uint64_t>(v));
    }
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), expected);
}

TEST(FFI, ManyMixedArgsStackSpill) {
    std::vector<EKind> kinds(9, EKind::I64);
    kinds.insert(kinds.end(), 9, EKind::F64);
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(many_mixed),
        EKind::F64, EStructKind::None, 0,
        kinds, std::vector<EStructKind>(18, EStructKind::None)));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args;
    double expected = 0;
    for (int n = 1; n <= 9; ++n) {
        args.push_back(static_cast<uint64_t>(n));
        expected += n;
    }
    for (int n = 0; n < 9; ++n) {
        double v = 1.5 + n;
        args.push_back(std::bit_cast<uint64_t>(v));
        expected += v;
    }
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), expected);
}

TEST(FFI, StructArgUnderGprPressure) {
    TPoint p{100, 200};
    std::vector<EKind> kinds(7, EKind::I64);
    kinds.push_back(EKind::Struct);
    std::vector<EStructKind> structs(7, EStructKind::None);
    structs.push_back(EStructKind::IntInt);
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(seven_then_point),
        EKind::I64, EStructKind::None, 0, kinds, structs));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args = {1, 2, 3, 4, 5, 6, 7, reinterpret_cast<uint64_t>(&p)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 28 + 300);
}

TEST(FFI, HfaArgUnderFpPressure) {
    TDPoint p{1.25, 2.75};
    std::vector<EKind> kinds(7, EKind::F64);
    kinds.push_back(EKind::Struct);
    std::vector<EStructKind> structs(7, EStructKind::None);
    structs.push_back(EStructKind::SseSse);
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(seven_d_then_dpoint),
        EKind::F64, EStructKind::None, 0, kinds, structs));
    ASSERT_NE(func, nullptr);
    std::vector<uint64_t> args;
    double expected = 0;
    for (int n = 1; n <= 7; ++n) {
        double v = n;
        args.push_back(std::bit_cast<uint64_t>(v));
        expected += v;
    }
    args.push_back(reinterpret_cast<uint64_t>(&p));
    expected += p.x + p.y;
    EXPECT_DOUBLE_EQ(LoadArg<double>((*func)(args.data(), args.size())), expected);
}

TEST(FFI, StructArgTwoChar) {
    // {char, char} is one INTEGER eightbyte; back it with a full eightbyte so
    // the by-value deref reads a valid slot.
    union {
        TTwoChar t;
        int64_t raw;
    } u{};
    u.raw = 0;
    u.t.a = 3;
    u.t.b = 4;
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(two_char_sum),
        EKind::I64, EStructKind::None, 0, {EKind::Struct}, {EStructKind::Int}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&u.t)};
    EXPECT_EQ(LoadArg<int64_t>((*func)(args.data(), args.size())), 7);
}

TEST(FFI, StructReturnTwoChar) {
    union {
        TTwoChar t;
        int64_t raw;
    } out{};
    out.raw = 0;
    auto func = std::unique_ptr<IFunction>(BuildFFI(reinterpret_cast<void*>(two_char_make),
        EKind::Struct, EStructKind::Int, 0, {EKind::I64, EKind::I64}, {EStructKind::None, EStructKind::None}));
    std::vector<uint64_t> args = {reinterpret_cast<uint64_t>(&out.t), 3, 4};
    (*func)(args.data(), args.size());
    EXPECT_EQ(out.t.a, 3);
    EXPECT_EQ(out.t.b, 4);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
