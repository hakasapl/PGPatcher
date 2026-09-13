#include "util/TaskTracker.hpp"

#include "util/Logger.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <utility>

TaskTracker::TaskTracker(std::string taskName,
                         const size_t& totalJobs,
                         const int& progressPrintModulo)
    : m_progressPrintModulo(progressPrintModulo)
    , m_taskName(std::move(taskName))
    , m_totalJobs(totalJobs)

{
    initJobStatus();
}

void TaskTracker::setCallbackFunc(std::function<void(size_t,
                                                     size_t)> callbackFunc)
{
    m_callbackFunc = std::move(callbackFunc);
}

void TaskTracker::completeJob(const Result& result)
{
    // Use lock_guard to make this method thread-safe.
    const std::scoped_lock lock(m_numJobsCompletedMutex);

    m_numJobsCompleted[result]++;
    m_totalRanJobs++;
    printJobStatus();
}

void TaskTracker::initJobStatus()
{
    m_lastPerc = 0;

    // Initialize all known Result values.
    m_numJobsCompleted[Result::FAILURE] = 0;
    m_numJobsCompleted[Result::Success] = 0;
    m_numJobsCompleted[Result::SuccessWithWarnings] = 0;

    Logger::info("{} Starting...", m_taskName);
}

void TaskTracker::printJobStatus(bool force)
{
    size_t combinedJobs = getCompletedJobs();
    size_t perc = combinedJobs * fullPercentage / m_totalJobs;
    if (force || perc != m_lastPerc) {
        m_lastPerc = perc;

        if (perc % m_progressPrintModulo == 0) {
            Logger::info("{} Progress: {}/{} [{}%]", m_taskName, combinedJobs, m_totalJobs, perc);

            // Callback.
            if (m_callbackFunc)
                m_callbackFunc(m_totalRanJobs, m_totalJobs);
        }
    }

    if (perc == fullPercentage)
        printJobSummary();
}

void TaskTracker::printJobSummary()
{
    // Print each job status Result.
    std::string outputLog = m_taskName + " Summary: ";
    for (const auto& pair : m_numJobsCompleted) {
        if (pair.second > 0) {
            const std::string stateStr = m_resultStr[pair.first];
            outputLog += "[ " + stateStr + " : " + std::to_string(pair.second) + " ] ";
        }
    }
    outputLog += "See log to see error messages, if any.";
    Logger::info(outputLog);
}

auto TaskTracker::getCompletedJobs() -> size_t
{
    // Initialize the Sum variable.
    size_t sum = 0;

    // Iterate through the unordered_map and sum the values.
    for (const auto& pair : m_numJobsCompleted)
        sum += pair.second;

    return sum;
}

auto TaskTracker::isCompleted() -> bool
{
    const std::scoped_lock lock(m_numJobsCompletedMutex);

    return getCompletedJobs() == m_totalJobs;
}

void TaskTracker::updateResult(Result& result,
                               const Result& currentResult,
                               const Result& threshold)
{
    if (currentResult > result) {
        if (currentResult > threshold)
            result = threshold;
        else
            result = currentResult;
    }
}
