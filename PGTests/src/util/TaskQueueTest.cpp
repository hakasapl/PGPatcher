#include "util/TaskQueue.hpp"

#include "TestUtil.hpp"
#include "util/ExceptionHandler.hpp"

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
#include <thread>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::uniqueMessage;
using TestUtil::waitFor;

class TaskQueueTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ExceptionHandler::reset();
        TaskQueue::setExceptionCallback(nullptr);
    }

    void TearDown() override
    {
        TaskQueue::setExceptionCallback(nullptr);
        ExceptionHandler::setTaskGuard(&ExceptionHandler::cppTraceGuard); // the guard main() installs
        ExceptionHandler::reset();
    }
};

TEST_F(TaskQueueTest,
       RunsTasksInOrderOnOneWorkerThread)
{
    TaskQueue queue;
    std::mutex mutex;
    std::vector<int> order;
    std::set<std::thread::id> threads;
    for (int i = 0; i < 50; i++) {
        queue.queueTask([&mutex, &order, &threads, i] {
            const std::scoped_lock lock(mutex);
            order.push_back(i);
            threads.insert(std::this_thread::get_id());
        });
    }
    queue.waitForCompletion();

    std::vector<int> expected(50);
    std::iota(expected.begin(), expected.end(), 0);
    EXPECT_EQ(order, expected);
    EXPECT_EQ(threads.size(), 1U);
    EXPECT_FALSE(threads.contains(std::this_thread::get_id()));
}

TEST_F(TaskQueueTest,
       ReportsQueuedAndProcessingState)
{
    TaskQueue queue;
    EXPECT_FALSE(queue.isWorking());
    EXPECT_FALSE(queue.isProcessing());
    EXPECT_FALSE(queue.isShutdown());
    EXPECT_EQ(queue.queuedTaskCount(), 0U);

    std::atomic<bool> release { false };
    queue.queueTask([&release] {
        while (!release)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    ASSERT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
    queue.queueTask([] { });
    queue.queueTask([] { });
    EXPECT_TRUE(queue.isWorking());
    EXPECT_TRUE(queue.isProcessing());
    EXPECT_EQ(queue.queuedTaskCount(), 2U);

    release = true;
    queue.waitForCompletion();
    EXPECT_FALSE(queue.isWorking());
    EXPECT_FALSE(queue.isProcessing());
    EXPECT_EQ(queue.queuedTaskCount(), 0U);
    EXPECT_FALSE(queue.isShutdown());
}

TEST_F(TaskQueueTest,
       ShutdownStopsTheWorkerAndDropsLaterTasks)
{
    TaskQueue queue;
    queue.shutdown();
    EXPECT_TRUE(queue.isShutdown());

    std::atomic<int> ran { 0 };
    queue.queueTask([&ran] { ran++; });
    EXPECT_EQ(queue.queuedTaskCount(), 0U);
    EXPECT_FALSE(queue.isWorking());
    queue.waitForCompletion();
    EXPECT_EQ(ran, 0);

    // A second shutdown is harmless.
    queue.shutdown();
    EXPECT_TRUE(queue.isShutdown());
}

TEST_F(TaskQueueTest,
       ShutdownWaitsForTheRunningTask)
{
    std::atomic<bool> finished { false };
    TaskQueue queue;
    queue.queueTask([&finished] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        finished = true;
    });
    ASSERT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
    queue.shutdown();
    EXPECT_TRUE(finished);
    EXPECT_TRUE(queue.isShutdown());
}

TEST_F(TaskQueueTest,
       DestructorJoinsTheWorker)
{
    std::atomic<bool> finished { false };
    {
        TaskQueue queue;
        queue.queueTask([&finished] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            finished = true;
        });
        ASSERT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
    }
    EXPECT_TRUE(finished);
}

TEST_F(TaskQueueTest,
       TaskExceptionIsRecordedAndStopsTheQueue)
{
    std::atomic<int> callbacks { 0 };
    TaskQueue::setExceptionCallback([&callbacks] { callbacks++; });

    const auto message = uniqueMessage("task boom");
    {
        TaskQueue queue;
        std::atomic<int> ran { 0 };
        queue.queueTask([&ran] { ran++; });
        queue.queueTask([message] { throw std::runtime_error(message); });
        ASSERT_TRUE(waitFor([] { return ExceptionHandler::hasException(); }));
        EXPECT_TRUE(waitFor([&queue] { return queue.isShutdown(); }));
        EXPECT_EQ(callbacks, 1);
        EXPECT_EQ(ran, 1);

        // Nothing runs on a stopped queue.
        queue.queueTask([&ran] { ran++; });
        EXPECT_EQ(queue.queuedTaskCount(), 0U);
        queue.waitForCompletion();
        EXPECT_EQ(ran, 1);
    }

    ExceptionHandler::throwExceptionOnMainThread();
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, message));
    TaskQueue::setExceptionCallback(nullptr);
}

TEST_F(TaskQueueTest,
       TasksAreDroppedWhileAnExceptionIsRecorded)
{
    TaskQueue queue;
    ExceptionHandler::setException(std::runtime_error(uniqueMessage("pre-existing")), "");

    std::atomic<int> ran { 0 };
    queue.queueTask([&ran] { ran++; });
    EXPECT_EQ(queue.queuedTaskCount(), 0U);
    EXPECT_FALSE(queue.isWorking());

    // Once the exception is cleared the queue accepts tasks again.
    ExceptionHandler::reset();
    queue.queueTask([&ran] { ran++; });
    queue.waitForCompletion();
    EXPECT_EQ(ran, 1);
}

TEST_F(TaskQueueTest,
       CarriesOnWhenTheGuardDoesNotRecordTheException)
{
    // A guard like the PGLite runtime's: the exception is reported, not recorded.
    ExceptionHandler::setTaskGuard([](const std::function<void()>& task) {
        try {
            task();
        } catch (const std::exception&) {
            return false;
        }

        return true;
    });

    std::atomic<int> ran { 0 };
    std::atomic<int> callbacks { 0 };
    TaskQueue::setExceptionCallback([&callbacks] { callbacks++; });
    {
        TaskQueue queue;
        queue.queueTask([&ran] { ran++; });
        queue.queueTask([] { throw std::runtime_error("task failed"); });
        queue.queueTask([&ran] { ran++; });
        queue.waitForCompletion();
    }

    EXPECT_EQ(ran, 2);
    EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(ExceptionHandler::hasException());
}

TEST_F(TaskQueueTest,
       IsWorkingUntilTheLastTaskHasRun)
{
    // The worker takes a task off the queue and then runs it. A thread that asks in between must not see an idle
    // queue, or waitForCompletion() returns before the task ran. Asking without a pause hits that moment often.
    TaskQueue queue;
    for (int attempt = 0; attempt < 5000; attempt++) {
        std::atomic<bool> hasRun = false;
        queue.queueTask([&hasRun] { hasRun = true; });
        while (queue.isWorking()) {
            // Ask again at once.
        }
        ASSERT_TRUE(hasRun) << "attempt " << attempt;
    }
}

}
