#include "ParallaxGenWarnings.hpp"

#include "util/Logger.hpp"

#include "PGGlobals.hpp"
#include <spdlog/spdlog.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace std;

// statics
unordered_map<std::wstring, unordered_set<pair<wstring, wstring>, ParallaxGenWarnings::PairHash>>
    ParallaxGenWarnings::s_mismatchWarnTracker;
mutex ParallaxGenWarnings::s_mismatchWarnTrackerMutex;

void ParallaxGenWarnings::init() { s_mismatchWarnTracker.clear(); }

void ParallaxGenWarnings::mismatchWarn(const wstring& matchedPath, const wstring& baseTex)
{
    auto* const pgd = PGGlobals::getPGD();

    // construct key
    auto matchedPathMod = pgd->getMod(matchedPath);
    auto baseTexMod = pgd->getMod(baseTex);

    if (matchedPathMod == nullptr || baseTexMod == nullptr) {
        return;
    }

    if (matchedPathMod == baseTexMod) {
        return;
    }

    {
        const std::scoped_lock lock(s_mismatchWarnTrackerMutex);
        s_mismatchWarnTracker[matchedPathMod->name].insert(std::make_pair(matchedPath, baseTex));
    }
}

void ParallaxGenWarnings::printWarnings()
{
    auto* const pgd = PGGlobals::getPGD();

    if (!s_mismatchWarnTracker.empty()) {
        Logger::warn("Potential mismatches were found, if you see any issues in-game please refer to this list to find "
                     "the culprit. PGPatcher cannot determine whether a mismatch is intended or not. Enable debug "
                     "logging to see each file that triggers this warning for each case.");
    }

    for (const auto& matchedMod : s_mismatchWarnTracker) {
        Logger::warn(L"\"{}\" assets are used in combination with:", matchedMod.first);
        for (auto texturePair : matchedMod.second) {
            Logger::warn(L"  - \"{}\"", texturePair.first);
            auto baseTexMod = pgd->getMod(texturePair.second);
            Logger::debug(L"  - {} used with {} from \"{}\"", texturePair.first, texturePair.second, baseTexMod->name);
        }
    }
}

void ParallaxGenWarnings::meshWarn(const wstring& matchedPath, const wstring& nifPath)
{
    auto* const pgd = PGGlobals::getPGD();

    // construct key
    auto matchedPathMod = pgd->getMod(matchedPath);
    auto nifPathMod = pgd->getMod(nifPath);

    if (matchedPathMod == nullptr || nifPathMod == nullptr) {
        return;
    }

    if (matchedPathMod == nifPathMod) {
        return;
    }

    const int priority = nifPathMod->priority;
    if (priority < 0) {
        return;
    }

    auto key = make_pair(matchedPathMod->name, nifPathMod->name);

    // Issue debug log
    Logger::debug(L"[Potential Mesh Mismatch] Matched path {} from mod {} were used on mesh {} from mod {}",
        matchedPath, matchedPathMod->name, nifPath, nifPathMod->name);
}
