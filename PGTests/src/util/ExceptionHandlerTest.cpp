#include "util/ExceptionHandler.hpp"

#include "TestUtil.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <stdexcept>
#include <string>
#include <thread>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::uniqueMessage;

class ExceptionHandlerTest : public ::testing::Test {
protected:
    void SetUp() override { ExceptionHandler::reset(); }
    void TearDown() override
    {
        ExceptionHandler::setTaskGuard(&ExceptionHandler::cppTraceGuard); // the guard main() installs
        ExceptionHandler::reset();
    }
};

TEST_F(ExceptionHandlerTest,
       StartsWithoutAnException)
{
    EXPECT_FALSE(ExceptionHandler::hasException());
    // Nothing is logged when there is nothing to report.
    const auto before = TestLog::count(spdlog::level::critical, "An unhandled exception occurred");
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_EQ(TestLog::count(spdlog::level::critical, "An unhandled exception occurred"), before);
}

TEST_F(ExceptionHandlerTest,
       RecordsOnlyTheFirstException)
{
    const auto first = uniqueMessage("first exception");
    const auto second = uniqueMessage("second exception");
    ExceptionHandler::setException(std::runtime_error(first), "trace one");
    EXPECT_TRUE(ExceptionHandler::hasException());
    ExceptionHandler::setException(std::runtime_error(second), "trace two");

    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, first));
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, "trace one"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::critical, second));
    EXPECT_FALSE(TestLog::contains(spdlog::level::critical, "trace two"));
}

TEST_F(ExceptionHandlerTest,
       ReportsFromTheMainThreadOnly)
{
    ExceptionHandler::setException(std::runtime_error(uniqueMessage("thread check")), "");

    bool threw = false;
    std::thread([&threw] {
        try {
            ExceptionHandler::throwExceptionOnMainThread();
        } catch (const std::runtime_error&) {
            threw = true;
        }
    }).join();
    EXPECT_TRUE(threw);

    EXPECT_NO_THROW(ExceptionHandler::throwExceptionOnMainThread());
}

TEST_F(ExceptionHandlerTest,
       ExceptionsSetFromWorkerThreadsAreVisible)
{
    const auto message = uniqueMessage("worker exception");
    std::thread([&message] { ExceptionHandler::setException(std::logic_error(message), "worker trace"); }).join();
    EXPECT_TRUE(ExceptionHandler::hasException());
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, message));
}

TEST_F(ExceptionHandlerTest,
       ResetForgetsTheException)
{
    const auto message = uniqueMessage("reset exception");
    ExceptionHandler::setException(std::runtime_error(message), "");
    ASSERT_TRUE(ExceptionHandler::hasException());
    ExceptionHandler::reset();
    EXPECT_FALSE(ExceptionHandler::hasException());

    // A new exception is recorded after the reset.
    const auto next = uniqueMessage("next exception");
    ExceptionHandler::setException(std::runtime_error(next), "");
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, next));
    EXPECT_FALSE(TestLog::contains(spdlog::level::critical, message));
}

TEST_F(ExceptionHandlerTest,
       WithoutATaskGuardRunGuardedRecordsTheExceptionWithoutATrace)
{
    // The tests run with cppTraceGuard() installed; the fallback is what a program without cpptrace gets.
    ExceptionHandler::setTaskGuard(nullptr);
    EXPECT_TRUE(ExceptionHandler::runGuarded([] { }));
    EXPECT_FALSE(ExceptionHandler::hasException());

    const auto message = uniqueMessage("guarded failure");
    EXPECT_FALSE(ExceptionHandler::runGuarded([&message] { throw std::runtime_error(message); }));

    EXPECT_TRUE(ExceptionHandler::hasException());
    ExceptionHandler::setMainThread();
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, message));
}

}
