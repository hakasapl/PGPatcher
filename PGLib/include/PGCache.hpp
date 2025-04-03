#pragma once

#include <NifFile.hpp>

#include <mutex>

#include <nlohmann/json.hpp>

#include "NIFUtil.hpp"
#include "ParallaxGenDirectory.hpp"
#include "patchers/base/PatcherUtil.hpp"

class PGCache {
private:
    // NIF cache
    static std::mutex s_nifCacheMutex;
    static nlohmann::json s_nifCache;

    // Other statics
    static ParallaxGenDirectory* s_pgd;
    static bool s_cacheEnabled;

public:
    // Structs
    struct NIFShapeMeta {
        std::unordered_map<NIFUtil::ShapeShader, bool> canApply;
        PatcherUtil::ShaderPatcherMatch winningMatch;
        NIFUtil::TextureSet textures;
    };

    struct NIFMeta {
        bool fromCache;
        unsigned int oldCRC32;
        unsigned int newCRC32;
        std::vector<NIFShapeMeta> shapes;
    };

    // Methods
    static void loadStatics(ParallaxGenDirectory* pgd, const bool& cacheEnabled);

    // cache functions
    static void loadCache(const nlohmann::json& cache);
    static void clearCache();
    static auto getCache() -> nlohmann::json;

    // meta functions
    static auto getNIFMeta(const std::filesystem::path& nifPath, nifly::NifFile* nif) -> NIFMeta;
    static void setNIFMeta(const std::filesystem::path& nifPath, const NIFMeta& meta);

    // save/load NIF functions
    static auto loadNIF(const std::filesystem::path& nifPath, nifly::NifFile* nif, unsigned int* oldCRC32 = nullptr)
        -> bool;
    static auto saveNIF(const std::filesystem::path& nifPath, nifly::NifFile* nif, unsigned int* newCRC32 = nullptr)
        -> bool;
};
