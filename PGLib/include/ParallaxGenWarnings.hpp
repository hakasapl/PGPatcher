#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

class ParallaxGenWarnings {
private:
    /**
     * @brief Struct that stores hash function for pair of wstrings (used for trackers)
     */
    struct PairHash {
        auto operator()(const std::pair<std::wstring, std::wstring>& p) const -> size_t
        {
            std::hash<std::wstring> hashWstr;
            return hashWstr(p.first) ^ (hashWstr(p.second) << 1);
        }
    };

    // Trackers for warnings
    static std::unordered_map<std::wstring, std::unordered_set<std::pair<std::wstring, std::wstring>, PairHash>>
        s_mismatchWarnTracker;
    static std::mutex s_mismatchWarnTrackerMutex; /** Mutex for MismatchWarnDebugTracker */

public:
    static void init();

    static void mismatchWarn(const std::wstring& matchedPath, const std::wstring& baseTex);

    static void meshWarn(const std::wstring& matchedPath, const std::wstring& nifPath);

    /// @brief print a summary of the already gathered warnings
    static void printWarnings();
};
