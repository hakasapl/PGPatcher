#include "util/TaskTracker.hpp"

#include "TestUtil.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <cstddef>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::uniqueMessage;

TEST(TaskTrackerTest,
     CountsCompletedJobs)
{
    TaskTracker tracker(uniqueMessage("Count"), 3);
    EXPECT_FALSE(tracker.isCompleted());
    tracker.completeJob(TaskTracker::Result::Success);
    tracker.completeJob(TaskTracker::Result::SuccessWithWarnings);
    EXPECT_FALSE(tracker.isCompleted());
    tracker.completeJob(TaskTracker::Result::Failure);
    EXPECT_TRUE(tracker.isCompleted());
}

TEST(TaskTrackerTest,
     CallbackFiresWheneverTheLoggedPercentageChanges)
{
    std::vector<std::pair<size_t, size_t>> calls;
    TaskTracker tracker(uniqueMessage("Callback"), 4);
    tracker.setCallbackFunc([&calls](size_t done, size_t total) { calls.emplace_back(done, total); });
    for (int i = 0; i < 4; i++)
        tracker.completeJob(TaskTracker::Result::Success);

    const std::vector<std::pair<size_t, size_t>> expected { { 1, 4 }, { 2, 4 }, { 3, 4 }, { 4, 4 } };
    EXPECT_EQ(calls, expected);
}

TEST(TaskTrackerTest,
     ProgressModuloLimitsTheCallbacks)
{
    std::vector<std::pair<size_t, size_t>> calls;
    TaskTracker tracker(uniqueMessage("Modulo"), 4, 50);
    tracker.setCallbackFunc([&calls](size_t done, size_t total) { calls.emplace_back(done, total); });
    for (int i = 0; i < 4; i++)
        tracker.completeJob(TaskTracker::Result::Success);

    // Only 50% and 100% are divisible by 50.
    const std::vector<std::pair<size_t, size_t>> expected { { 2, 4 }, { 4, 4 } };
    EXPECT_EQ(calls, expected);
}

TEST(TaskTrackerTest,
     UnchangedPercentageDoesNotFireTheCallback)
{
    int calls = 0;
    TaskTracker tracker(uniqueMessage("Unchanged"), 1000);
    tracker.setCallbackFunc([&calls](size_t, size_t) { calls++; });
    // 1 to 9 of 1000 all round down to 0%.
    for (int i = 0; i < 9; i++)
        tracker.completeJob(TaskTracker::Result::Success);
    EXPECT_EQ(calls, 0);
    tracker.completeJob(TaskTracker::Result::Success); // 1%
    EXPECT_EQ(calls, 1);
}

TEST(TaskTrackerTest,
     LogsStartProgressAndSummary)
{
    const auto name = uniqueMessage("Logged");
    TaskTracker tracker(name, 2);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, name + " Starting..."));

    tracker.completeJob(TaskTracker::Result::Success);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, name + " Progress: 1/2 [50%]"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::info, name + " Summary:"));

    tracker.completeJob(TaskTracker::Result::Failure);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, name + " Progress: 2/2 [100%]"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, name + " Summary:"));

    // The summary lists only the results that occurred.
    bool foundSummary = false;
    for (const auto& entry : TestLog::entries()) {
        if (entry.message.find(name + " Summary:") == std::string::npos)
            continue;

        foundSummary = true;
        EXPECT_NE(entry.message.find("[ COMPLETED : 1 ]"), std::string::npos);
        EXPECT_NE(entry.message.find("[ FAILED : 1 ]"), std::string::npos);
        EXPECT_EQ(entry.message.find("WARNINGS"), std::string::npos);
    }
    EXPECT_TRUE(foundSummary);
}

TEST(TaskTrackerTest,
     UpdateResultEscalatesWithoutExceedingTheThreshold)
{
    using Result = TaskTracker::Result;

    Result result = Result::Success;
    TaskTracker::updateResult(result, Result::Success);
    EXPECT_EQ(result, Result::Success);

    TaskTracker::updateResult(result, Result::SuccessWithWarnings);
    EXPECT_EQ(result, Result::SuccessWithWarnings);

    // Never downgrades.
    TaskTracker::updateResult(result, Result::Success);
    EXPECT_EQ(result, Result::SuccessWithWarnings);

    TaskTracker::updateResult(result, Result::Failure);
    EXPECT_EQ(result, Result::Failure);

    // The threshold caps the escalation.
    Result capped = Result::Success;
    TaskTracker::updateResult(capped, Result::Failure, Result::SuccessWithWarnings);
    EXPECT_EQ(capped, Result::SuccessWithWarnings);

    Result unchanged = Result::Success;
    TaskTracker::updateResult(unchanged, Result::Failure, Result::Success);
    EXPECT_EQ(unchanged, Result::Success);
}

TEST(TaskTrackerTest,
     ZeroJobsIsCompleteFromTheStart)
{
    TaskTracker tracker(uniqueMessage("Empty"), 0);
    EXPECT_TRUE(tracker.isCompleted());
    // Completing a job that was never expected must not divide by zero.
    EXPECT_NO_THROW(tracker.completeJob(TaskTracker::Result::Success));
}

TEST(TaskTrackerTest,
     CompleteJobIsThreadSafe)
{
    constexpr int numThreads = 8;
    constexpr int jobsPerThread = 250;
    TaskTracker tracker(uniqueMessage("Threads"), static_cast<size_t>(numThreads) * jobsPerThread);

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&tracker] {
            for (int i = 0; i < jobsPerThread; i++)
                tracker.completeJob(TaskTracker::Result::Success);
        });
    }
    for (auto& thread : threads)
        thread.join();

    EXPECT_TRUE(tracker.isCompleted());
}

}
