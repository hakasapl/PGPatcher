#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>

#include <nlohmann/json.hpp>

class PGCache {
private:
    // NIF cache
    static std::mutex s_nifCacheMutex;
    static nlohmann::json s_nifCache;

    static std::mutex s_texCacheMutex;
    static nlohmann::json s_texCache;

    static bool s_cacheEnabled;

    enum class CacheType : uint8_t { NIF, TEX };

public:
    // Enable or disable cache
    static void enableCache(bool enable);
    static auto isCacheEnabled() -> bool;

    // Global loading and saving functions
    static auto loadNIFCache(const nlohmann::json& cacheData) -> bool;
    static auto saveNIFCache() -> nlohmann::json;

    static auto loadTexCache(const nlohmann::json& cacheData) -> bool;
    static auto saveTexCache() -> nlohmann::json;

    // TEX cache functions
    static auto getTEXCache(const std::filesystem::path& relPath, nlohmann::json& cacheData) -> bool;
    static void setTEXCache(
        const std::filesystem::path& relPath, const nlohmann::json& cacheData, const size_t& mtime = 0);

    // NIF cache functions
    static auto getNIFCache(const std::filesystem::path& relPath, nlohmann::json& cacheData) -> bool;
    static void setNIFCache(
        const std::filesystem::path& relPath, const nlohmann::json& cacheData, const size_t& mtime = 0);

private:
    // helpers
    static auto getFileCache(const std::filesystem::path& relPath, nlohmann::json& cacheData, const CacheType& type)
        -> bool;
    static void setFileCache(const std::filesystem::path& relPath, const nlohmann::json& cacheData,
        const CacheType& type, const size_t& mtime = 0);
};
