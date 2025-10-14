#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

class ParallaxGenWarnings {
private:
    struct MismatchWarnInfo {
        std::wstring matchedPath;
        std::wstring matchedFromPath;
        std::wstring matchedFromMod;

        auto operator==(const MismatchWarnInfo& other) const -> bool
        {
            return matchedPath == other.matchedPath && matchedFromPath == other.matchedFromPath
                && matchedFromMod == other.matchedFromMod;
        }
    };

    struct MismatchWarnInfoHash {
        auto operator()(const MismatchWarnInfo& info) const -> size_t
        {
            const size_t h1 = std::hash<std::wstring>()(info.matchedPath);
            const size_t h2 = std::hash<std::wstring>()(info.matchedFromPath);
            const size_t h3 = std::hash<std::wstring>()(info.matchedFromMod);

            // Combine hashes using bit manipulation
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    // Trackers for warnings
    static std::unordered_map<std::wstring, std::unordered_set<MismatchWarnInfo, MismatchWarnInfoHash>>
        s_mismatchWarnTracker;
    static std::mutex s_mismatchWarnTrackerMutex; /** Mutex for MismatchWarnDebugTracker */

public:
    static void init();

    static void mismatchWarn(const std::wstring& matchedPath, const std::wstring& baseTex);

    static void meshWarn(const std::wstring& matchedPath, const std::wstring& nifPath);

    /// @brief print a summary of the already gathered warnings
    static void printWarnings();
};
