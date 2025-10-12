#include "ParallaxGenTask.hpp"

#include <spdlog/spdlog.h>

#include <cstddef>
#include <mutex>
#include <string>
#include <utility>

using namespace std;

ParallaxGenTask::ParallaxGenTask(string taskName, const size_t& totalJobs, const int& progressPrintModulo)
    : m_progressPrintModulo(progressPrintModulo)
    , m_taskName(std::move(taskName))
    , m_totalJobs(totalJobs)
{
    initJobStatus();
}

void ParallaxGenTask::completeJob(const PGResult& result)
{
    // Use lock_guard to make this method thread-safe
    const lock_guard<mutex> lock(m_numJobsCompletedMutex);

    m_numJobsCompleted[result]++;
    printJobStatus();
}

void ParallaxGenTask::initJobStatus()
{
    m_lastPerc = 0;

    // Initialize all known PGResult values
    m_numJobsCompleted[PGResult::FAILURE] = 0;
    m_numJobsCompleted[PGResult::SUCCESS] = 0;
    m_numJobsCompleted[PGResult::SUCCESS_WITH_WARNINGS] = 0;

    spdlog::info("{} Starting...", m_taskName);
}

void ParallaxGenTask::printJobStatus(bool force)
{
    size_t combinedJobs = getCompletedJobs();
    size_t perc = combinedJobs * FULL_PERCENTAGE / m_totalJobs;
    if (force || perc != m_lastPerc) {
        m_lastPerc = perc;

        if (perc % m_progressPrintModulo == 0) {
            spdlog::info("{} Progress: {}/{} [{}%]", m_taskName, combinedJobs, m_totalJobs, perc);
        }
    }

    if (perc == FULL_PERCENTAGE) {
        printJobSummary();
    }
}

void ParallaxGenTask::printJobSummary()
{
    // Print each job status Result
    string outputLog = m_taskName + " Summary: ";
    for (const auto& pair : m_numJobsCompleted) {
        if (pair.second > 0) {
            const string stateStr = m_pgResultStr[pair.first];
            outputLog += "[ " + stateStr + " : " + to_string(pair.second) + " ] ";
        }
    }
    outputLog += "See log to see error messages, if any.";
    spdlog::info(outputLog);
}

auto ParallaxGenTask::getCompletedJobs() -> size_t
{
    // Initialize the Sum variable
    size_t sum = 0;

    // Iterate through the unordered_map and sum the values
    for (const auto& pair : m_numJobsCompleted) {
        sum += pair.second;
    }

    return sum;
}

auto ParallaxGenTask::isCompleted() -> bool
{
    const lock_guard<mutex> lock(m_numJobsCompletedMutex);

    return getCompletedJobs() == m_totalJobs;
}

void ParallaxGenTask::updatePGResult(PGResult& result, const PGResult& currentResult, const PGResult& threshold)
{
    if (currentResult > result) {
        if (currentResult > threshold) {
            result = threshold;
        } else {
            result = currentResult;
        }
    }
}
