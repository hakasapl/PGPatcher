#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

/**
 * @brief Tracks progress and outcome statistics for a named batch of parallel jobs.
 *
 * Thread-safe: completeJob() may be called concurrently from multiple threads.
 * Progress messages and a summary are emitted via Logger at configurable intervals.
 */
class TaskTracker {
public:
    /** @brief Possible outcomes for an individual job. */
    enum class Result : uint8_t { SUCCESS, SUCCESS_WITH_WARNINGS, FAILURE };

private:
    static constexpr int FULL_PERCENTAGE = 100;

    int m_progressPrintModulo;

    std::string m_taskName;
    size_t m_totalJobs;
    size_t m_lastPerc = 0;
    std::mutex m_numJobsCompletedMutex;

    size_t m_totalRanJobs;
    std::function<void(size_t, size_t)> m_callbackFunc;

    std::unordered_map<Result, size_t> m_numJobsCompleted;

    std::unordered_map<Result, std::string> m_ResultStr = {
        {Result::SUCCESS, "COMPLETED"},
        {Result::SUCCESS_WITH_WARNINGS, "COMPLETED WITH WARNINGS"},
        {Result::FAILURE, "FAILED"},
    };

public:
    /**
     * @brief Constructs a TaskTracker and logs a start message.
     *
     * @param taskName Human-readable name used in log output.
     * @param totalJobs Expected total number of jobs to track.
     * @param progressPrintModulo Progress is logged only when the percentage is divisible by this value.
     */
    TaskTracker(std::string taskName,
                const size_t& totalJobs,
                const int& progressPrintModulo = 1);

    /**
     * @brief Registers a callback invoked on every logged progress update.
     *
     * @param callbackFunc Callable receiving (completedJobs, totalJobs) counts.
     */
    void setCallbackFunc(std::function<void(size_t,
                                            size_t)> callbackFunc);

    /**
     * @brief Records the completion of one job with the given result and updates progress.
     *
     * Thread-safe; may be called from multiple threads simultaneously.
     *
     * @param result Outcome of the completed job.
     */
    void completeJob(const Result& result);

    /**
     * @brief Returns whether all expected jobs have been completed.
     *
     * @return true if the number of completed jobs equals totalJobs, false otherwise.
     */
    [[nodiscard]] auto isCompleted() -> bool;

    /**
     * @brief Escalates result to currentResult if it is more severe, capped at threshold.
     *
     * Allows aggregating individual job results into a single overall result without
     * exceeding a configured severity ceiling.
     *
     * @param result Aggregate result to potentially update.
     * @param currentResult The new individual result to incorporate.
     * @param threshold Maximum severity allowed (defaults to FAILURE).
     */
    static void updateResult(Result& result,
                             const Result& currentResult,
                             const Result& threshold = Result::FAILURE);

private:
    void initJobStatus();
    void printJobStatus(bool force = false);
    void printJobSummary();
    [[nodiscard]] auto getCompletedJobs() -> size_t;
};
