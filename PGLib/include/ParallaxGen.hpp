#pragma once

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <filesystem>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <spdlog/spdlog.h>
#include <string>

#include <boost/functional/hash.hpp>

#include "ParallaxGenDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "ParallaxGenTask.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/NIFUtil.hpp"

class ParallaxGen {
private:
    // Registered Patchers
    static PatcherUtil::PatcherTextureSet s_texPatchers;
    static PatcherUtil::PatcherMeshSet s_meshPatchers;

    static std::shared_mutex s_diffJSONMutex;
    static nlohmann::json s_diffJSON;

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
    // Task Structs
    struct NifFileResult {
        nifly::NifFile nifFile;
        std::vector<ParallaxGenPlugin::TXSTResult> txstResults;
        std::unordered_map<int, NIFUtil::ShapeShader> shadersAppliedMesh;
        std::vector<std::tuple<int, int, std::string>> idxCorrections;
    };

    struct DDSFileResult {
        DirectX::ScratchImage ddsImage;
    };

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
    static auto processNIF(const std::filesystem::path& nifPath, const nifly::NifFile& origNif, const bool& patchPlugin,
        std::unordered_map<std::filesystem::path, NifFileResult>& createdNIFs, bool& nifModified,
        const std::unordered_map<int, NIFUtil::ShapeShader>* forceShaders = nullptr,
        const std::unordered_map<int, NIFUtil::ShapeShader>* origShadersApplied = nullptr) -> bool;

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
        const std::unordered_map<NIFUtil::ShapeShader, bool>& canApply,
        const PatcherUtil::PatcherMeshObjectSet& patchers, NIFUtil::ShapeShader& shaderApplied,
        const NIFUtil::ShapeShader* forceShader = nullptr) -> bool;

    static auto getMeshesFromPluginResults(const std::unordered_map<int, NIFUtil::ShapeShader>& shadersAppliedMesh,
        const std::unordered_map<nifly::NiShape*, int>& shapeIdxs,
        const std::unordered_map<int, std::unordered_map<int, ParallaxGenPlugin::TXSTResult>>& recordHandleTracker)
        -> std::unordered_map<int,
            std::pair<std::vector<ParallaxGenPlugin::TXSTResult>, std::unordered_map<int, NIFUtil::ShapeShader>>>;

    static auto createNIFPatcherObjects(const std::filesystem::path& nifPath, nifly::NifFile* nif)
        -> PatcherUtil::PatcherMeshObjectSet;

    static auto getDuplicateNIFPath(const std::filesystem::path& nifPath, const int& index) -> std::filesystem::path;

    // DDS Runners
    static auto patchDDS(const std::filesystem::path& ddsPath) -> ParallaxGenTask::PGResult;

    static auto createDDSPatcherObjects(const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds)
        -> PatcherUtil::PatcherTextureObjectSet;
};
