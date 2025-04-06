#include "PGCache.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenUtil.hpp"

// statics
std::mutex PGCache::s_nifCacheMutex;
nlohmann::json PGCache::s_nifCache = nlohmann::json::object();

bool PGCache::s_cacheEnabled = true;
auto PGCache::enableCache(bool enable) -> void { s_cacheEnabled = enable; }
auto PGCache::isCacheEnabled() -> bool { return s_cacheEnabled; }

auto PGCache::getNIFCache(const std::filesystem::path& nifPath, nlohmann::json& cacheData) -> bool
{
    cacheData.clear();
    const auto cacheKey = ParallaxGenUtil::utf16toUTF8(nifPath.wstring());

    // check if cache exists and pull it
    {
        const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
        if (s_nifCache.contains(cacheKey)) {
            cacheData = s_nifCache[cacheKey];
        } else {
            return false;
        }
    }

    // check if cache should be invalidated
    auto* const pgd = PGGlobals::getPGD();
    if (!pgd->isFile(nifPath)) {
        return false;
    }

    // invalidate if invalidation fields are missing
    if (!cacheData.contains("mtime")) {
        return false;
    }

    // Check file modified time
    const auto fileMTime = pgd->getFileMTime(nifPath);
    const auto cacheMTime = cacheData["mtime"].get<size_t>();
    if (fileMTime != cacheMTime) {
        return false; // NOLINT(readability-simplify-boolean-expr)
    }

    return true;
}

auto PGCache::setNIFCache(const std::filesystem::path& nifPath, const nlohmann::json& nifData, const size_t& mtime)
    -> void
{
    const auto cacheKey = ParallaxGenUtil::utf16toUTF8(nifPath.wstring());

    // set file modified time and size
    auto* const pgd = PGGlobals::getPGD();
    nlohmann::json cacheData = nifData;
    if (mtime > 0) {
        cacheData["mtime"] = mtime;
    } else {
        cacheData["mtime"] = pgd->getFileMTime(nifPath);
    }

    // set cache data
    {
        const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
        s_nifCache[cacheKey] = cacheData;
    }
}

void PGCache::loadNIFCache(nlohmann::json cacheData)
{
    const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
    s_nifCache = std::move(cacheData);
}

auto PGCache::saveNIFCache() -> nlohmann::json
{
    const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
    return s_nifCache;
}
