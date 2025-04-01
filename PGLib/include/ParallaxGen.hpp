#pragma once

#include <NifFile.hpp>
#include <filesystem>
#include <miniz.h>
#include <mutex>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_set>

#include <boost/functional/hash.hpp>

#include "NIFUtil.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenTask.hpp"
#include "patchers/base/PatcherUtil.hpp"

class ParallaxGen {
private:
    static constexpr int MESHES_LENGTH = 7;
    static constexpr int PROGRESS_INTERVAL_CONFLICTS = 10;

    std::filesystem::path m_outputDir; // ParallaxGen output directory

    // Dependency objects
    ParallaxGenDirectory* m_pgd;
    ParallaxGenD3D* m_pgd3D;

    // sort blocks enabled, optimize disabled (for now)
    nifly::NifSaveOptions m_nifSaveOptions = { .optimize = false, .sortBlocks = false };

    struct ShapeKey {
        std::wstring nifPath;
        int shapeIndex;

        // Equality operator to compare two ShapeKey objects
        auto operator==(const ShapeKey& other) const -> bool
        {
            return nifPath == other.nifPath && shapeIndex == other.shapeIndex;
        }
    };

    // Runner vars
    PatcherUtil::PatcherTextureSet m_texPatchers;
    PatcherUtil::PatcherMeshSet m_meshPatchers;
    std::unordered_map<std::wstring, int>* m_modPriority;

    // Define a hash function for ShapeKey
    struct ShapeKeyHash {
        auto operator()(const ShapeKey& key) const -> size_t
        {
            // Hash the nifPath and ShapeIndex individually
            const std::size_t h1 = std::hash<std::wstring> {}(key.nifPath);
            const std::size_t h2 = std::hash<int> {}(key.shapeIndex);

            return h1 ^ (h2 << 1); // shifting to reduce collisions
        }
    };

    std::unordered_map<ShapeKey, std::vector<PatcherUtil::ShaderPatcherMatch>, ShapeKeyHash> m_allowedShadersCache;
    std::mutex m_allowedShadersCacheMutex;

public:
    //
    // The following methods are called from main.cpp and are public facing
    //

    // constructor
    ParallaxGen(std::filesystem::path outputDir, ParallaxGenDirectory* pgd, ParallaxGenD3D* pgd3D,
        const bool& optimizeMeshes = false);
    void loadPatchers(
        const PatcherUtil::PatcherMeshSet& meshPatchers, const PatcherUtil::PatcherTextureSet& texPatchers);
    void loadModPriorityMap(std::unordered_map<std::wstring, int>* modPriority);
    // enables parallax on relevant meshes
    void patch(const bool& multiThread = true, const bool& patchPlugin = true);
    // Dry run for finding potential matches (used with mod manager integration)
    [[nodiscard]] auto findModConflicts(const bool& multiThread = true, const bool& patchPlugin = true)
        -> std::unordered_map<std::wstring,
            std::tuple<std::set<NIFUtil::ShapeShader>, std::unordered_set<std::wstring>>>;
    // zips all meshes and removes originals
    void zipMeshes() const;
    // deletes entire output folder
    void deleteOutputDir(const bool& preOutput = true) const;
    // get output zip name
    [[nodiscard]] static auto getOutputZipName() -> std::filesystem::path;
    // get diff json name
    [[nodiscard]] static auto getDiffJSONName() -> std::filesystem::path;

private:
    // thread safe JSON update
    static void threadSafeJSONUpdate(
        const std::function<void(nlohmann::json&)>& operation, nlohmann::json& j, std::mutex& mutex);

    // processes a NIF file (enable parallax if needed)
    auto processNIF(const std::filesystem::path& nifFile, nlohmann::json* diffJSON, std::mutex* diffJSONMutex,
        const bool& patchPlugin = true, PatcherUtil::ConflictModResults* conflictMods = nullptr)
        -> ParallaxGenTask::PGResult;

    // TODO this should return bool
    auto processNIF(const std::filesystem::path& nifFile, const std::vector<std::byte>& nifBytes, bool& nifModified,
        const std::unordered_map<int, NIFUtil::ShapeShader>* forceShaders = nullptr,
        std::vector<std::pair<std::filesystem::path, nifly::NifFile>>* dupNIFs = nullptr,
        const bool& patchPlugin = true, PatcherUtil::ConflictModResults* conflictMods = nullptr) -> nifly::NifFile;

    // processes a shape within a NIF file
    auto processShape(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape* nifShape,
        const int& shapeIndex, PatcherUtil::PatcherMeshObjectSet& patchers, NIFUtil::ShapeShader& shaderApplied,
        PatcherUtil::ConflictModResults* conflictMods = nullptr, const NIFUtil::ShapeShader* forceShader = nullptr)
        -> bool;

    auto processDDS(const std::filesystem::path& ddsFile) -> ParallaxGenTask::PGResult;

    // Zip methods
    void addFileToZip(
        mz_zip_archive& zip, const std::filesystem::path& filePath, const std::filesystem::path& zipPath) const;

    void zipDirectory(const std::filesystem::path& dirPath, const std::filesystem::path& zipPath) const;
};
