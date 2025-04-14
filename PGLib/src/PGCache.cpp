#include "PGCache.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenUtil.hpp"

// statics
std::mutex PGCache::s_nifCacheMutex;
nlohmann::json PGCache::s_nifCache = nlohmann::json::object();

std::mutex PGCache::s_texCacheMutex;
nlohmann::json PGCache::s_texCache = nlohmann::json::object();

bool PGCache::s_cacheEnabled = true;
auto PGCache::enableCache(bool enable) -> void { s_cacheEnabled = enable; }
auto PGCache::isCacheEnabled() -> bool { return s_cacheEnabled; }

auto PGCache::getTEXCache(const std::filesystem::path& relPath, nlohmann::json& cacheData) -> bool
{
    return getFileCache(relPath, cacheData, CacheType::TEX);
}

void PGCache::setTEXCache(const std::filesystem::path& relPath, const nlohmann::json& cacheData, const size_t& mtime)
{
    setFileCache(relPath, cacheData, CacheType::TEX, mtime);
}

auto PGCache::getNIFCache(const std::filesystem::path& relPath, nlohmann::json& cacheData) -> bool
{
    return getFileCache(relPath, cacheData, CacheType::NIF);
}

void PGCache::setNIFCache(const std::filesystem::path& relPath, const nlohmann::json& cacheData, const size_t& mtime)
{
    setFileCache(relPath, cacheData, CacheType::NIF, mtime);
}

auto PGCache::getFileCache(const std::filesystem::path& relPath, nlohmann::json& cacheData, const CacheType& type)
    -> bool
{
    cacheData.clear();
    const auto cacheKey = ParallaxGenUtil::utf16toUTF8(relPath.wstring());

    // check if cache exists and pull it
    if (type == CacheType::TEX) {
        const std::lock_guard<std::mutex> lock(s_texCacheMutex);
        if (s_texCache.contains(cacheKey)) {
            cacheData = s_texCache[cacheKey];
        } else {
            return false;
        }
    } else if (type == CacheType::NIF) {
        const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
        if (s_nifCache.contains(cacheKey)) {
            cacheData = s_nifCache[cacheKey];
        } else {
            return false;
        }
    }

    // check if cache should be invalidated
    auto* const pgd = PGGlobals::getPGD();
    if (!pgd->isFile(relPath)) {
        return false;
    }

    // invalidate if invalidation fields are missing
    if (!cacheData.contains("mtime")) {
        return false;
    }

    // Check file modified time
    const auto fileMTime = pgd->getFileMTime(relPath);
    const auto cacheMTime = cacheData["mtime"].get<size_t>();
    if (fileMTime != cacheMTime) {
        return false; // NOLINT(readability-simplify-boolean-expr)
    }

    return true;
}

void PGCache::setFileCache(
    const std::filesystem::path& relPath, const nlohmann::json& cacheData, const CacheType& type, const size_t& mtime)
{
    const auto cacheKey = ParallaxGenUtil::utf16toUTF8(relPath.wstring());

    // set file modified time and size
    auto* const pgd = PGGlobals::getPGD();
    nlohmann::json saveCacheData = cacheData;
    if (mtime > 0) {
        saveCacheData["mtime"] = mtime;
    } else {
        saveCacheData["mtime"] = pgd->getFileMTime(relPath);
    }

    // set cache data
    if (type == CacheType::TEX) {
        const std::lock_guard<std::mutex> lock(s_texCacheMutex);
        s_texCache[cacheKey] = saveCacheData;
    } else if (type == CacheType::NIF) {
        const std::lock_guard<std::mutex> lock(s_nifCacheMutex);
        s_nifCache[cacheKey] = saveCacheData;
    }
}

auto PGCache::loadNIFCache(const nlohmann::json& cacheData) -> bool
{
    const std::lock_guard<std::mutex> lock(s_nifCacheMutex);

    // check if cache is valid
    if (!cacheData.contains("version")) {
        return false;
    }

    const auto version = cacheData["version"].get<std::string>();
    if (version != PG_VERSION) {
        // invalidate cache on version change
        return false;
    }

    s_nifCache = cacheData;
    return true;
}

auto PGCache::saveNIFCache() -> nlohmann::json
{
    const std::lock_guard<std::mutex> lock(s_nifCacheMutex);

    // add version to cache
    nlohmann::json saveCacheData = s_nifCache;
    saveCacheData["version"] = PG_VERSION;

    return saveCacheData;
}

auto PGCache::loadTexCache(const nlohmann::json& cacheData) -> bool
{
    const std::lock_guard<std::mutex> lock(s_texCacheMutex);

    // check if cache is valid
    if (!cacheData.contains("version")) {
        return false;
    }

    const auto version = cacheData["version"].get<std::string>();
    if (version != PG_VERSION) {
        // invalidate cache on version change
        return false;
    }

    s_texCache = cacheData;
    return true;
}

auto PGCache::saveTexCache() -> nlohmann::json
{
    const std::lock_guard<std::mutex> lock(s_texCacheMutex);

    // add version to cache
    nlohmann::json saveCacheData = s_texCache;
    saveCacheData["version"] = PG_VERSION;

    return saveCacheData;
}
