#include "ParallaxGenWarnings.hpp"

#include "util/Logger.hpp"

#include "PGGlobals.hpp"
#include <spdlog/spdlog.h>

#include <fmt/format.h>
#include <fmt/xchar.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

using namespace std;

// statics
std::unordered_map<std::wstring,
    std::unordered_set<ParallaxGenWarnings::MismatchWarnInfo, ParallaxGenWarnings::MismatchWarnInfoHash>>
    ParallaxGenWarnings::s_mismatchWarnTracker;
mutex ParallaxGenWarnings::s_mismatchWarnTrackerMutex;

void ParallaxGenWarnings::init() { s_mismatchWarnTracker.clear(); }

void ParallaxGenWarnings::mismatchWarn(const wstring& matchedPath, const wstring& baseTex)
{
    auto* const mmd = PGGlobals::getMMD();
    auto* const pgd = PGGlobals::getPGD();

    // construct key
    auto matchedPathMod = mmd->getModByFile(pgd->getModLookupFile(matchedPath));
    auto baseTexMod = mmd->getModByFile(pgd->getModLookupFile(baseTex));

    if (matchedPathMod == nullptr || baseTexMod == nullptr) {
        return;
    }

    if (matchedPathMod == baseTexMod) {
        return;
    }

    {
        const std::scoped_lock lock(s_mismatchWarnTrackerMutex);
        s_mismatchWarnTracker[matchedPathMod->name].insert({ matchedPath, baseTex, baseTexMod->name });
    }
}

void ParallaxGenWarnings::meshWarn(const wstring& matchedPath, const wstring& nifPath)
{
    auto* const mmd = PGGlobals::getMMD();
    auto* const pgd = PGGlobals::getPGD();

    // construct key
    auto matchedPathMod = mmd->getModByFile(pgd->getModLookupFile(matchedPath));
    auto nifPathMod = mmd->getModByFile(pgd->getModLookupFile(nifPath));

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

void ParallaxGenWarnings::printWarnings()
{
    if (s_mismatchWarnTracker.empty()) {
        return;
    }

    unordered_set<wstring> printedMods;

    wstring warnMsg
        = L"Potential mismatches were found, if you see any issues in-game please refer to this list to find "
          "the culprit. PGPatcher cannot determine whether a mismatch is intended or not. Enable debug "
          "logging to see each file that triggers this warning for each case.\n\n";
    wstring dbgMsg;
    for (const auto& [mod, mismatches] : s_mismatchWarnTracker) {
        warnMsg += mod + L" assets are used in combination with:\n";
        for (const auto& mismatch : mismatches) {
            if (printedMods.insert(mismatch.matchedFromMod).second) {
                // only print once per mod combination
                warnMsg += L"  - " + mismatch.matchedFromMod + L"\n";
            }

            dbgMsg += fmt::format(L"  - {} used with {} from \"{}\"\n", mismatch.matchedPath, mismatch.matchedFromPath,
                mismatch.matchedFromMod);
        }
    }

    if (!warnMsg.empty()) {
        Logger::warn(warnMsg);
    }

    if (!dbgMsg.empty()) {
        Logger::debug(L"Potential mismatches details:\n{}", dbgMsg);
    }
}
