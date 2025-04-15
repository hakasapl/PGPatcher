#pragma once

#include <mutex>
#include <unordered_map>
#include <unordered_set>

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
    static std::unordered_map<std::wstring, std::unordered_set<std::wstring>>
        s_mismatchWarnTracker; // matched mod to set of base mods
    static std::mutex s_mismatchWarnTrackerMutex; /** Mutex for MismatchWarnTracker */
    static std::unordered_map<std::wstring, std::unordered_set<std::pair<std::wstring, std::wstring>, PairHash>>
        s_mismatchWarnDebugTracker;
    static std::mutex s_mismatchWarnDebugTrackerMutex; /** Mutex for MismatchWarnDebugTracker */

    static std::unordered_set<std::pair<std::wstring, std::wstring>, PairHash>
        s_meshWarnTracker; /** Keeps tabs on WARN mesh mismatch messages to avoid duplicates */
    static std::mutex s_meshWarnTrackerMutex; /** Mutex for MeshWarnTracker */
    static std::unordered_set<std::pair<std::wstring, std::wstring>, PairHash>
        s_meshWarnDebugTracker; /** Keeps tabs on DEBUG mesh mismatch messages to avoid duplicates */
    static std::mutex s_meshWarnDebugTrackerMutex; /** Mutex for MeshWarnDebugTracker */

public:
    static void init();

    static void mismatchWarn(const std::wstring& matchedPath, const std::wstring& baseTex);

    static void meshWarn(const std::wstring& matchedPath, const std::wstring& nifPath);

    /// @brief print a summary of the already gathered warnings
    static void printWarnings();
};
