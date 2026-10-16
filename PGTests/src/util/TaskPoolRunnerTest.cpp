#include "util/TaskPoolRunner.hpp"

#include "TestUtil.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::uniqueMessage;

class TaskPoolRunnerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ExceptionHandler::reset();
        TaskPoolRunner::setExceptionCallback(nullptr);
    }

    void TearDown() override
    {
        TaskPoolRunner::setExceptionCallback(nullptr);
        ExceptionHandler::setTaskGuard(&ExceptionHandler::cppTraceGuard); // the guard main() installs
        ExceptionHandler::reset();
    }
};

TEST_F(TaskPoolRunnerTest,
       PoolThreadCountKeepsTwoThreadsFreeButAtLeastOne)
{
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(0), 2U); // unknown concurrency counts as four
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(1), 1U);
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(2), 1U);
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(3), 1U);
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(4), 2U);
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(8), 6U);
    EXPECT_EQ(TaskPoolRunner::poolThreadCount(32), 30U);
}

TEST_F(TaskPoolRunnerTest,
       SingleThreadedRunsTasksInOrderOnTheCallingThread)
{
    TaskPoolRunner runner(false);
    std::vector<int> order;
    std::set<std::thread::id> threads;
    for (int i = 0; i < 20; i++) {
        runner.addTask([&order, &threads, i] {
            order.push_back(i);
            threads.insert(std::this_thread::get_id());
        });
    }
    runner.runTasks();

    std::vector<int> expected(20);
    std::iota(expected.begin(), expected.end(), 0);
    EXPECT_EQ(order, expected);
    EXPECT_EQ(threads, (std::set<std::thread::id> { std::this_thread::get_id() }));
    EXPECT_FALSE(ExceptionHandler::hasException());
}

TEST_F(TaskPoolRunnerTest,
       MultiThreadedRunsEveryTaskOffTheCallingThread)
{
    TaskPoolRunner runner(true);
    std::atomic<int> count { 0 };
    std::mutex mutex;
    std::set<std::thread::id> threads;
    for (int i = 0; i < 200; i++) {
        runner.addTask([&count, &mutex, &threads] {
            count++;
            const std::scoped_lock lock(mutex);
            threads.insert(std::this_thread::get_id());
        });
    }
    runner.runTasks();

    EXPECT_EQ(count, 200);
    EXPECT_FALSE(threads.empty());
    EXPECT_FALSE(threads.contains(std::this_thread::get_id()));
    EXPECT_FALSE(ExceptionHandler::hasException());
}

TEST_F(TaskPoolRunnerTest,
       RunTasksWithoutTasksReturns)
{
    TaskPoolRunner single(false);
    single.runTasks();
    TaskPoolRunner multi(true);
    multi.runTasks();
    SUCCEED();
}

TEST_F(TaskPoolRunnerTest,
       SingleThreadedExceptionStopsTheBatch)
{
    std::atomic<int> callbacks { 0 };
    TaskPoolRunner::setExceptionCallback([&callbacks] { callbacks++; });

    const auto message = uniqueMessage("pool boom");
    TaskPoolRunner runner(false);
    std::atomic<int> ran { 0 };
    runner.addTask([&ran] { ran++; });
    runner.addTask([message] { throw std::runtime_error(message); });
    runner.addTask([&ran] { ran++; });
    runner.runTasks();

    EXPECT_EQ(ran, 1);
    EXPECT_TRUE(ExceptionHandler::hasException());
    EXPECT_EQ(callbacks, 1);
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, message));
    TaskPoolRunner::setExceptionCallback(nullptr);
}

TEST_F(TaskPoolRunnerTest,
       MultiThreadedExceptionIsRecordedAndRunTasksReturns)
{
    std::atomic<int> callbacks { 0 };
    TaskPoolRunner::setExceptionCallback([&callbacks] { callbacks++; });

    const auto message = uniqueMessage("pool mt boom");
    TaskPoolRunner runner(true);
    runner.addTask([message] { throw std::runtime_error(message); });
    for (int i = 0; i < 20; i++)
        runner.addTask([] { std::this_thread::sleep_for(std::chrono::milliseconds(1)); });
    runner.runTasks();

    EXPECT_TRUE(ExceptionHandler::hasException());
    EXPECT_EQ(callbacks, 1);
    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, message));
    TaskPoolRunner::setExceptionCallback(nullptr);
}

TEST_F(TaskPoolRunnerTest,
       MultiThreadedTasksAreSkippedWhileAnExceptionIsRecorded)
{
    ExceptionHandler::setException(std::runtime_error(uniqueMessage("pre-existing")), "");
    TaskPoolRunner runner(true);
    std::atomic<int> ran { 0 };
    for (int i = 0; i < 5; i++)
        runner.addTask([&ran] { ran++; });
    runner.runTasks();
    EXPECT_EQ(ran, 0);
}

TEST_F(TaskPoolRunnerTest,
       LogMessagesOfEveryTaskAreFlushed)
{
    const auto tag = uniqueMessage("pool log");
    TaskPoolRunner runner(true);
    for (int i = 0; i < 10; i++)
        runner.addTask([tag, i] { Logger::info("{} task {}", tag, i); });
    runner.runTasks();

    for (int i = 0; i < 10; i++)
        EXPECT_TRUE(TestLog::contains(spdlog::level::info, tag + " task " + std::to_string(i)));
}

/// @brief A guard like the PGLite runtime's: the exception is reported, not recorded.
bool reportingGuard(const std::function<void()>& task)
{
    try {
        task();
    } catch (const std::exception&) {
        return false;
    }

    return true;
}

TEST_F(TaskPoolRunnerTest,
       SingleThreadedCarriesOnWhenTheGuardDoesNotRecordTheException)
{
    ExceptionHandler::setTaskGuard(&reportingGuard);
    TaskPoolRunner runner(false);
    std::vector<int> ran;
    runner.addTask([&ran] { ran.push_back(0); });
    runner.addTask([] { throw std::runtime_error("task failed"); });
    runner.addTask([&ran] { ran.push_back(2); });
    int callbacks = 0;
    TaskPoolRunner::setExceptionCallback([&callbacks] { callbacks++; });
    runner.runTasks();

    EXPECT_EQ(ran, (std::vector<int> { 0, 2 }));
    EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(ExceptionHandler::hasException());
}

TEST_F(TaskPoolRunnerTest,
       PoolCarriesOnWhenTheGuardDoesNotRecordTheException)
{
    ExceptionHandler::setTaskGuard(&reportingGuard);
    TaskPoolRunner runner(true);
    std::atomic<int> ran { 0 };
    for (int i = 0; i < 20; i++)
        if (i == 5)
            runner.addTask([] { throw std::runtime_error("task failed"); });
        else
            runner.addTask([&ran] { ran++; });
    std::atomic<int> callbacks { 0 };
    TaskPoolRunner::setExceptionCallback([&callbacks] { callbacks++; });
    runner.runTasks();

    EXPECT_EQ(ran, 19);
    EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(ExceptionHandler::hasException());
}

}
