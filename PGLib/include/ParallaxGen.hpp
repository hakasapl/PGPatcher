#pragma once

#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenTask.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include <DirectXTex.h>
#include <boost/container_hash/hash.hpp>
#include <boost/functional/hash.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

class ParallaxGen {
private:
    // Registered Patchers
    static PatcherUtil::PatcherTextureSet s_texPatchers;
    static PatcherUtil::PatcherMeshSet s_meshPatchers;

    static std::shared_mutex s_diffJSONMutex;
    static nlohmann::json s_diffJSON;

    struct MatchCacheKey {
        std::wstring nifPath;
        NIFUtil::TextureSet slots;
        bool singlepassMATO = false;

        auto operator==(const MatchCacheKey& other) const -> bool
        {
            return slots == other.slots && singlepassMATO == other.singlepassMATO && nifPath == other.nifPath;
        }
    };

    struct MatchCacheKeyHasher {
        auto operator()(const MatchCacheKey& key) const -> size_t
        {
            size_t seed = 0;
            boost::hash_combine(seed, boost::hash_range(key.slots.begin(), key.slots.end()));
            boost::hash_combine(seed, key.singlepassMATO);
            return seed;
        }
    };

    static std::shared_mutex s_matchCacheMutex;
    static std::unordered_map<MatchCacheKey, std::vector<PatcherUtil::ShaderPatcherMatch>, MatchCacheKeyHasher>
        s_matchCache;

public:
    /**
     * @brief Allows patchers to be registered and used in the patching process.
     *
     * @param meshPatchers mesh patchers
     * @param texPatchers texture patchers
     */
    static void loadPatchers(
        const PatcherUtil::PatcherMeshSet& meshPatchers, const PatcherUtil::PatcherTextureSet& texPatchers);

    /**
     * @brief Run patcher (this is the meat of this whole thing)
     *
     * @param multiThread whether to use multithreading
     * @param patchPlugin whether to generate plugin patches
     */
    static void patch(const bool& multiThread = true, const bool& patchPlugin = true);

    /**
     * @brief Finalize any other requires output files
     */
    static void finalize();

    /**
     * @brief Populates the mod conflicts in MMD as well as shader types for each mod
     *
     * @param multiThread whether to use multithreading
     * @param patchPlugin whether to generate plugin patches
     */
    static void populateModData(const bool& multiThread = true, const bool& patchPlugin = true);

    /**
     * @brief Delets output directory in a smart way
     *
     * @param preOutput whether this is being called pre or post output
     */
    static void deleteOutputDir(const bool& preOutput = true);

    /**
     * @brief Check if the output directory is empty
     *
     * @return true if the output directory is empty
     * @return false if the output directory is not empty
     */
    static auto isOutputEmpty() -> bool;

    static auto getDiffJSON() -> nlohmann::json;

private:
    // NIF Runners

    /**
     * @brief Patch a single NIF file
     *
     * @param nifPath relative path to the NIF file
     * @param patchPlugin whether to generate plugin patches
     * @param dryRun whether to run in dry run mode (no changes made)
     * @return ParallaxGenTask::PGResult result of the patching process
     */
    static auto patchNIF(const std::filesystem::path& nifPath, const bool& patchPlugin) -> ParallaxGenTask::PGResult;

    static auto populateModInfoFromNIF(const std::filesystem::path& nifPath,
        const ParallaxGenDirectory::NifCache& nifCache, const bool& patchPlugin) -> ParallaxGenTask::PGResult;

    // NIF Helpers

    /**
     * @brief Process a single NIF file
     *
     * @param nifPath relative path to the NIF file
     * @param nifBytes NIF file bytes (used for CRC calculation)
     * @param patchPlugin whether to generate plugin patches
     * @param dryRun whether to run in dry run mode (no changes made)
     * @param[in,out] nifCache NIF cache JSON object to populate
     * @param[out] createdNIFs map of created NIFs
     * @param[out] nifModified whether the NIF file was modified
     * @param forceShaders optional map of shape shaders to force (used for duplicate meshes in recursion)
     * @return true if the NIF file was processed successfully
     * @return false if the NIF file was not processed successfully
     */
    static auto processNIF(const std::filesystem::path& nifPath, nifly::NifFile* nif, bool singlepassMATO,
        std::unordered_map<unsigned int, NIFUtil::TextureSet>& alternateTextures) -> bool;

    /**
     * @brief Process a single NIF shape
     *
     * @param nifPath relative path to the NIF file
     * @param nif loaded NIF file object
     * @param nifShape NIF shape object (pointer) to process
     * @param dryRun whether to run in dry run mode (no changes made)
     * @param[out] shapeCache NIF shape cache JSON object to populate
     * @param canApply map of shape shaders that can be applied to this shape
     * @param patchers patcher objects created from registered patchers
     * @param[out] shaderApplied shader that was applied to this shape
     * @param forceShader optional shape shader to force (used for duplicate meshes in recursion)
     * @return true if the NIF shape was processed successfully
     * @return false if the NIF shape was not processed successfully
     */
    static auto processNIFShape(const std::filesystem::path& nifPath, nifly::NifFile* nif, nifly::NiShape* nifShape,
        const PatcherUtil::PatcherMeshObjectSet& patchers, bool singlepassMATO,
        NIFUtil::TextureSet* alternateTexture = nullptr) -> bool;

    static auto getMatches(const std::wstring& nifPath, const NIFUtil::TextureSet& slots,
        const PatcherUtil::PatcherMeshObjectSet& patchers, const bool& dryRun, bool singlepassMATO,
        const PatcherUtil::PatcherMeshObjectSet* patcherObjects = nullptr, nifly::NiShape* shape = nullptr)
        -> std::vector<PatcherUtil::ShaderPatcherMatch>;

    /**
     * @brief Get the Winning Match object (checks mod priority)
     *
     * @param Matches Matches to check
     * @param NIFPath NIF path to check
     * @param ModPriority Mod priority map
     * @return ShaderPatcherMatch Winning match
     */
    static auto getWinningMatch(const std::vector<PatcherUtil::ShaderPatcherMatch>& matches)
        -> PatcherUtil::ShaderPatcherMatch;

    /**
     * @brief Helper method to run a transform if needed on a match
     *
     * @param Match Match to run transform
     * @param Patchers Patcher set to use
     * @return ShaderPatcherMatch Transformed match
     */
    static auto applyTransformIfNeeded(
        PatcherUtil::ShaderPatcherMatch& match, const PatcherUtil::PatcherMeshObjectSet& patchers) -> bool;

    static auto createNIFPatcherObjects(const std::filesystem::path& nifPath, nifly::NifFile* nif)
        -> PatcherUtil::PatcherMeshObjectSet;

    // DDS Runners
    static auto patchDDS(const std::filesystem::path& ddsPath) -> ParallaxGenTask::PGResult;

    static auto createDDSPatcherObjects(const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds)
        -> PatcherUtil::PatcherTextureObjectSet;
};
