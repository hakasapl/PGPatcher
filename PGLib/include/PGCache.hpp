#pragma once

#include <filesystem>
#include <mutex>

#include <nlohmann/json.hpp>

class PGCache {
private:
    // NIF cache
    static std::mutex s_nifCacheMutex;
    static nlohmann::json s_nifCache;

public:
    // Global loading and saving functions
    static void loadNIFCache(nlohmann::json cacheData);
    static auto saveNIFCache() -> nlohmann::json;

    // NIF functions
    static auto getNIFCache(const std::filesystem::path& nifPath, nlohmann::json& cacheData) -> bool;
    static void setNIFCache(const std::filesystem::path& nifPath, const nlohmann::json& nifData);
};
