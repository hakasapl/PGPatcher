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
    // The task holds the worker until the test releases it, so the test cannot miss it running, and it still runs for a
    // while after the release, so the shutdown comes while it runs.
    std::atomic<bool> isReleased { false };
    std::atomic<bool> hasFinished { false };
    TaskQueue queue;
    queue.queueTask([&isReleased, &hasFinished] {
        while (!isReleased)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        hasFinished = true;
    });
    EXPECT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
    isReleased = true;
    queue.shutdown();
    EXPECT_TRUE(hasFinished);
    EXPECT_TRUE(queue.isShutdown());
}

TEST_F(TaskQueueTest,
       DestructorJoinsTheWorker)
{
    // As in ShutdownWaitsForTheRunningTask, the task holds the worker until it is released and then still runs for a
    // while.
    std::atomic<bool> isReleased { false };
    std::atomic<bool> hasFinished { false };
    {
        TaskQueue queue;
        queue.queueTask([&isReleased, &hasFinished] {
            while (!isReleased)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            hasFinished = true;
        });
        EXPECT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
        isReleased = true;
    }
    EXPECT_TRUE(hasFinished);
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
    // The worker looks for a recorded exception before each task and stops for good when it finds one, so the
    // exception is recorded and cleared while a task keeps the worker busy.
    std::atomic<bool> isReleased { false };
    std::atomic<int> ran { 0 };
    TaskQueue queue;
    queue.queueTask([&isReleased] {
        while (!isReleased)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    EXPECT_TRUE(waitFor([&queue] { return queue.isProcessing(); }));
    ExceptionHandler::setException(std::runtime_error(uniqueMessage("pre-existing")), "");

    queue.queueTask([&ran] { ran++; });
    EXPECT_EQ(queue.queuedTaskCount(), 0U);

    // Once the exception is cleared the queue accepts tasks again.
    ExceptionHandler::reset();
    isReleased = true;
    EXPECT_TRUE(waitFor([&queue] { return !queue.isWorking(); }));
    EXPECT_EQ(ran, 0);
    queue.queueTask([&ran] { ran++; });
    EXPECT_TRUE(waitFor([&queue] { return !queue.isWorking(); }));
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
    std::atomic<bool> hasRun { false };
    TaskQueue queue;
    for (int attempt = 0; attempt < 5000; attempt++) {
        hasRun = false;
        queue.queueTask([&hasRun] { hasRun = true; });
        while (queue.isWorking()) {
            // Ask again at once.
        }
        ASSERT_TRUE(hasRun) << "attempt " << attempt;
    }
}

TEST_F(TaskQueueTest,
       IsWorkingUntilATaskQueuedByATaskHasRun)
{
    // A task that queues another one on its own queue is still running when the other one is queued, so the queue
    // must not look idle between the two.
    std::atomic<bool> hasRun { false };
    TaskQueue queue;
    for (int attempt = 0; attempt < 5000; attempt++) {
        hasRun = false;
        queue.queueTask([&queue, &hasRun] { queue.queueTask([&hasRun] { hasRun = true; }); });
        while (queue.isWorking()) {
            // Ask again at once.
        }
        ASSERT_TRUE(hasRun) << "attempt " << attempt;
    }
}

TEST_F(TaskQueueTest,
       SkipsAnEmptyTask)
{
    // An empty callable is skipped, and the queue does not stay busy because of it. Calling it would throw, which stops
    // the queue, so a later task would not run.
    std::atomic<bool> hasRun { false };
    TaskQueue queue;
    queue.queueTask(std::function<void()>());
    EXPECT_TRUE(waitFor([&queue] { return !queue.isWorking(); }));
    EXPECT_FALSE(queue.isProcessing());
    EXPECT_FALSE(ExceptionHandler::hasException());

    queue.queueTask([&hasRun] { hasRun = true; });
    EXPECT_TRUE(waitFor([&queue] { return !queue.isWorking(); }));
    EXPECT_TRUE(hasRun);
    EXPECT_EQ(queue.queuedTaskCount(), 0U);
}

TEST_F(TaskQueueTest,
       ShutsDownRightAfterTheLastTaskHasRun)
{
    // After a task the worker goes back to waiting for the next one. A shutdown in that moment must still wake it, or
    // the destructor waits for the worker forever. Destroying a new queue as soon as it is idle hits that moment within
    // a few attempts, so a lost wakeup makes this hang.
    for (int attempt = 0; attempt < 200; attempt++) {
        TaskQueue queue;
        queue.queueTask([] { });
        while (queue.isWorking()) {
            // Ask again at once.
        }
    }
}

}
