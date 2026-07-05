#pragma once

#include "PGDirectory.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/TaskTracker.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "util/TaskQueue.hpp"
#include <DirectXTex.h>
#include <boost/container_hash/hash.hpp>
#include <boost/functional/hash.hpp>
#include <miniz.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class PGPatcher {
public:
    // Mesh Patch Tracking structures (for meta info displayed to user later)
    struct MatchMeta {
        std::shared_ptr<PGModManager::Mod> mod;
        PGEnums::ShapeShader shader {};
        PGEnums::ShapeShader shaderTransformTo {};
        std::filesystem::path matchedPath;
    };
    struct MeshShapeMeta {
        uint32_t blockID;
        std::string shapeName;
        std::vector<std::string> prePatchersApplied;
        std::vector<std::string> postPatchersApplied;
        std::unordered_map<PGMeshPermutationTracker::FormKey,
                           std::vector<MatchMeta>,
                           PGMeshPermutationTracker::FormKeyHash>
            matches;
    };
    struct MeshMeta {
        std::vector<std::string> globalPatchersApplied;
        std::vector<PGMeshPermutationTracker::FormKey> formKeys;
        std::map<size_t, MeshShapeMeta> shapeMeta;
    };

    using MeshPatchInfo = std::map<std::filesystem::path, MeshMeta>;

private:
    // Registered Patchers
    static PatcherUtil::PatcherTextureSet s_texPatchers;
    static PatcherUtil::PatcherMeshSet s_meshPatchers;

    static std::shared_mutex s_diffJSONMutex;
    static nlohmann::json s_diffJSON;

    static inline MeshPatchInfo s_meshPatchInfo;
    static inline std::shared_mutex s_meshPatchInfoMutex;

public:
    /**
     * @brief Allows patchers to be registered and used in the patching process.
     *
     * @param meshPatchers mesh patchers
     * @param texPatchers texture patchers
     */
    static void loadPatchers(const PatcherUtil::PatcherMeshSet& meshPatchers,
                             const PatcherUtil::PatcherTextureSet& texPatchers);

    /**
     * @brief Run mesh patcher
     *
     * @param multiThread whether to use multithreading
     */
    static void patchMeshes(const bool& multiThread = true,
                            const bool& forceBasePatch = false,
                            const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes = {},
                            const bool& checkAllowedRecTypes = false,
                            const std::function<void(size_t,
                                                     size_t)>& progressCallback = {});

    /**
     * @brief Run texture patcher
     *
     * @param multiThread whether to use multithreading
     */
    static void patchTextures(const bool& multiThread = true,
                              const std::function<void(size_t,
                                                       size_t)>& progressCallback = {});

    /**
     * @brief Get the Patch Meta object
     *
     * @return std::map<std::filesystem::path, MeshMeta>
     */
    static auto getPatchMeta() -> MeshPatchInfo;

    /**
     * @brief Sort matches according to a provided mod priority list.
     *
     * Matches are ordered by mod priority first, then by shader quality, with stable ordering within ties.
     */
    static void sortMatches(std::vector<PatcherUtil::ShaderPatcherMatch>& matches);

    /**
     * @brief Sort mesh patch metadata matches according to a provided mod priority list.
     *
     * Matches are ordered by mod priority first, then by shader quality, with stable ordering within ties.
     */
    static void sortMatches(std::vector<MatchMeta>& matches,
                            const std::vector<std::shared_ptr<PGModManager::Mod>>& modPriorityList);

    /**
     * @brief Check whether any mesh patch metadata has been collected.
     *
     * @return true if patch metadata exists
     * @return false if no patch metadata exists
     */
    static auto hasConflictData() -> bool;

    /**
     * @brief Reset transient data collected during patching.
     *
     * Clears in-memory conflict metadata and diff JSON so subsequent patch runs
     * start from a clean state.
     */
    static void resetRunState();

    /**
     * @brief Finalize any other requires output files
     */
    static void finalize();

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
     * @return TaskTracker::Result result of the patching process
     */
    static auto patchNIF(const std::filesystem::path& nifPath,
                         TaskQueue& setModelUsesQueue,
                         const bool& forceBasePatch = false,
                         const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes = {},
                         const bool& checkAllowedRecTypes = false) -> TaskTracker::Result;

    // NIF Helpers

    /**
     * @brief Process a single NIF file
     *
     * @param nifPath relative path to the NIF file
     * @param nifBytes NIF file bytes (used for CRC calculation)
     * @param[in,out] nifCache NIF cache JSON object to populate
     * @param[out] createdNIFs map of created NIFs
     * @param[out] nifModified whether the NIF file was modified
     * @param forceShaders optional map of shape shaders to force (used for duplicate meshes in recursion)
     * @return true if the NIF file was processed successfully
     * @return false if the NIF file was not processed successfully
     */
    static auto processNIF(const std::filesystem::path& nifPath,
                           nifly::NifFile* nif,
                           MeshMeta& meshMeta,
                           bool singlepassMATO,
                           const PGMeshPermutationTracker::FormKey& formKey,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           std::unordered_map<unsigned int,
                                              PGTypes::TextureSet>& alternateTextures,
                           std::unordered_set<unsigned int>& nonAltTexShapes) -> bool;

    /**
     * @brief Process a single NIF shape
     *
     * @param nifPath relative path to the NIF file
     * @param nif loaded NIF file object
     * @param nifShape NIF shape object (pointer) to process
     * @param[out] shapeCache NIF shape cache JSON object to populate
     * @param canApply map of shape shaders that can be applied to this shape
     * @param patchers patcher objects created from registered patchers
     * @param[out] shaderApplied shader that was applied to this shape
     * @param forceShader optional shape shader to force (used for duplicate meshes in recursion)
     * @return true if the NIF shape was processed successfully
     * @return false if the NIF shape was not processed successfully
     */
    static auto processNIFShape(const std::filesystem::path& nifPath,
                                nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                MeshShapeMeta& meshShapeMeta,
                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                bool singlepassMATO,
                                const PGMeshPermutationTracker::FormKey& formKey,
                                const PGPlugin::ModelRecordType& modelRecordType,
                                PGTypes::TextureSet* alternateTexture = nullptr) -> bool;

    static auto getMatches(const PGTypes::TextureSet& slots,
                           const PatcherUtil::PatcherMeshObjectSet& patchers,
                           bool singlepassMATO,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           const PatcherUtil::PatcherMeshObjectSet* patcherObjects = nullptr,
                           nifly::NiShape* shape = nullptr) -> std::vector<PatcherUtil::ShaderPatcherMatch>;

    /**
     * @brief Helper method to run a transform if needed on a match
     *
     * @param Match Match to run transform
     * @param Patchers Patcher set to use
     * @return ShaderPatcherMatch Transformed match
     */
    static auto applyTransformIfNeeded(PatcherUtil::ShaderPatcherMatch& match,
                                       const PatcherUtil::PatcherMeshObjectSet& patchers) -> bool;

    static auto createNIFPatcherObjects(const std::filesystem::path& nifPath,
                                        nifly::NifFile* nif) -> PatcherUtil::PatcherMeshObjectSet;

    // DDS Runners
    static auto patchDDS(const std::filesystem::path& ddsPath) -> TaskTracker::Result;

    static auto createDDSPatcherObjects(const std::filesystem::path& ddsPath,
                                        DirectX::ScratchImage* dds) -> PatcherUtil::PatcherTextureObjectSet;
};
