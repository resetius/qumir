#include <gtest/gtest.h>

#include <qumir/future.h>

using namespace NQumir;

namespace {

TFuture<int> AsyncInt(int v) {
    co_return v;
}

TFuture<void> AsyncVoid() {
    co_return;
}

TFuture<int> AsyncThrow() {
    throw std::runtime_error("boom");
    co_return 0;
}

TFuture<int> AsyncSuspendingInt(int v) {
    co_await std::suspend_always{};
    co_return v;
}

} // namespace

TEST(FutureTest, WrapAndUnwrapInt) {
    TWrappedFuture<int> wrapped(AsyncInt(42));

    EXPECT_TRUE(wrapped.done());
    EXPECT_TRUE(wrapped.await_ready());

    int result = 0;
    wrapped.await_resume(&result);
    EXPECT_EQ(result, 42);
}

TEST(FutureTest, WrapAndUnwrapVoid) {
    TWrappedFuture<void> wrapped(AsyncVoid());

    EXPECT_TRUE(wrapped.done());
    EXPECT_TRUE(wrapped.await_ready());

    EXPECT_NO_THROW(wrapped.await_resume(nullptr));
}

TEST(FutureTest, ExceptionPropagatesThroughErasure) {
    TWrappedFuture<int> wrapped(AsyncThrow());

    EXPECT_TRUE(wrapped.await_ready());

    int result = 0;
    EXPECT_THROW(wrapped.await_resume(&result), std::runtime_error);
}

TEST(FutureTest, AwaitTypeErasedInt) {
    TWrappedFuture<int> wrapped(AsyncInt(99));
    TFuture<int> unwrapped = AwaitTypeErasedFuture<int>(&wrapped);

    EXPECT_TRUE(unwrapped.await_ready());
    EXPECT_EQ(unwrapped.await_resume(), 99);
}

TEST(FutureTest, AwaitTypeErasedExceptionRethrows) {
    TWrappedFuture<int> wrapped(AsyncThrow());
    TFuture<int> unwrapped = AwaitTypeErasedFuture<int>(&wrapped);

    EXPECT_TRUE(unwrapped.await_ready());
    EXPECT_THROW(unwrapped.await_resume(), std::runtime_error);
}

TEST(FutureTest, SuspendingWrapResumeUnwrap) {
    TWrappedFuture<int> wrapped(AsyncSuspendingInt(7));

    EXPECT_FALSE(wrapped.done());
    EXPECT_FALSE(wrapped.await_ready());

    wrapped.resume();

    EXPECT_TRUE(wrapped.done());
    EXPECT_TRUE(wrapped.await_ready());

    int result = 0;
    wrapped.await_resume(&result);
    EXPECT_EQ(result, 7);
}

TEST(FutureTest, SuspendingChainPropagatesOnResume) {
    TWrappedFuture<int> wrapped(AsyncSuspendingInt(13));
    TFuture<int> outer = AwaitTypeErasedFuture<int>(&wrapped);

    EXPECT_FALSE(wrapped.done());
    EXPECT_FALSE(outer.await_ready());

    wrapped.resume();

    EXPECT_TRUE(outer.await_ready());
    EXPECT_EQ(outer.await_resume(), 13);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
