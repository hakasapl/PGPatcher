#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>

class ParallaxGenTask {
public:
    enum class PGResult : uint8_t { SUCCESS, SUCCESS_WITH_WARNINGS, FAILURE };

private:
    static constexpr int FULL_PERCENTAGE = 100;

    int m_progressPrintModulo;

    std::string m_taskName;
    size_t m_totalJobs;
    size_t m_lastPerc = 0;
    std::mutex m_numJobsCompletedMutex;

    size_t m_totalRanJobs;
    std::function<void(size_t, size_t)> m_callbackFunc;

    std::unordered_map<PGResult, size_t> m_numJobsCompleted;

    std::unordered_map<PGResult, std::string> m_pgResultStr = { { PGResult::SUCCESS, "COMPLETED" },
        { PGResult::SUCCESS_WITH_WARNINGS, "COMPLETED WITH WARNINGS" }, { PGResult::FAILURE, "FAILED" } };

public:
    ParallaxGenTask(std::string taskName, const size_t& totalJobs, const int& progressPrintModulo = 1);

    void setCallbackFunc(std::function<void(size_t, size_t)> callbackFunc);

    void completeJob(const PGResult& result);
    [[nodiscard]] auto isCompleted() -> bool;

    static void updatePGResult(
        PGResult& result, const PGResult& currentResult, const PGResult& threshold = PGResult::FAILURE);

private:
    void initJobStatus();
    void printJobStatus(bool force = false);
    void printJobSummary();
    [[nodiscard]] auto getCompletedJobs() -> size_t;
};
