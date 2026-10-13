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
#include <DirectXTex.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class TaskQueue;

/**
 * @brief Runs the registered patchers over every mesh and texture of the load order.
 *
 * Meshes are patched once per plugin use (see PGMeshPermutationTracker), textures once. Alongside the output files it
 * collects the data the conflict viewer shows (patchMeta()) and the CRC diff of changed base meshes (diffJSON()). With
 * an update cache present (PGRunCache), meshes whose previous output is still valid are replayed instead of patched.
 *
 * The per-mesh pipeline, processNIF() and the functions it uses (PGPatcherPipeline.cpp), is shared with the PGLite
 * runtime, which patches a mesh when the game loads it; the runners that walk the load order and write files
 * (PGPatcher.cpp) belong to the offline programs.
 */
class PGPatcher {
public:
    // Mesh Patch Tracking structures (for meta info displayed to user later).
    /// @brief One shader match of a shape, as shown in the conflict viewer.
    struct MatchMeta {
        std::shared_ptr<PGModManager::Mod> mod;
        PGEnums::ShapeShader shader { };
        PGEnums::ShapeShader shaderTransformTo { };
        std::filesystem::path matchedPath;
        /// Owning mod of each non-empty result texture slot if this match is applied (tracked mods only)
        std::vector<std::pair<PGEnums::TextureSlots, std::shared_ptr<PGModManager::Mod>>> resultTextureMods;
    };
    /// @brief What was done to one shape of a mesh, with its shader matches per plugin use.
    struct MeshShapeMeta {
        uint32_t blockID = 0;
        std::string shapeName;
        std::vector<std::string> prePatchersApplied;
        std::vector<std::string> postPatchersApplied;
        std::unordered_map<PGMeshPermutationTracker::FormKey,
                           std::vector<MatchMeta>,
                           PGMeshPermutationTracker::FormKeyHash>
            matches;
    };
    /// @brief What was done to one mesh. shapeMeta is keyed by the shape's position among the patchable shapes.
    struct MeshMeta {
        std::vector<std::string> globalPatchersApplied;
        std::vector<PGMeshPermutationTracker::FormKey> formKeys;
        std::map<size_t, MeshShapeMeta> shapeMeta;
    };

    /// @brief Patch metadata of every patched mesh, keyed by lower case relative path.
    using MeshPatchInfo = std::map<std::filesystem::path, MeshMeta>;

    /// Stages of patchMeshes() that a caller may want to show separately in its progress UI.
    enum class MeshPatchStage : uint8_t { EvaluatingPreviousOutput, Patching };

private:
    // Registered Patchers.
    static PatcherUtil::PatcherTextureSet s_texPatchers;
    static PatcherUtil::PatcherMeshSet s_meshPatchers;

    static std::shared_mutex s_diffJSONMutex;
    static nlohmann::json s_diffJSON;

    static inline MeshPatchInfo s_meshPatchInfo;
    static inline std::shared_mutex s_meshPatchInfoMutex;

public:
    /**
     * @brief Registers the patcher factories used by patchMeshes() and patchTextures().
     *
     * @param meshPatchers mesh patchers
     * @param texPatchers texture patchers
     */
    static void loadPatchers(const PatcherUtil::PatcherMeshSet& meshPatchers,
                             const PatcherUtil::PatcherTextureSet& texPatchers);

    /**
     * @brief Patches every mesh of PGGlobals::pgd()->meshes() and applies the results to the plugins.
     *
     * Waits for the mesh use mapping and texture classification of PGDirectory first. Model uses are applied through
     * PGPlugin::setModelUses() and the Light Placer handler and weighted variant validation run at the end.
     *
     * @param shouldMultithread whether to patch on the task pool
     * @param forceBasePatch also patch meshes that no plugin uses (pgtools, which has no plugins)
     * @param allowedModelRecTypes plugin record types whose uses are patched, see checkAllowedRecTypes
     * @param checkAllowedRecTypes when true, a use of a record type not in allowedModelRecTypes is skipped and the
     * original mesh is kept for it
     * @param excludeFacegens whether to skip patching facegen meshes
     * @param progressCallback receives (completed, total) counts of the current stage
     * @param stageCallback invoked when a new stage starts. When a previous output is updated, every mesh is
     * evaluated before patching and the progress callback counts that evaluation first, so the stages are reported
     * separately for progress displays.
     */
    static void patchMeshes(const bool& shouldMultithread = true,
                            const bool& forceBasePatch = false,
                            const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes = { },
                            const bool& checkAllowedRecTypes = false,
                            const bool& excludeFacegens = false,
                            const std::function<void(size_t,
                                                     size_t)>& progressCallback = { },
                            const std::function<void(MeshPatchStage)>& stageCallback = { });

    /**
     * @brief Patches every texture of PGGlobals::pgd()->textures(): runs the texture hooks the mesh patchers
     * registered and the global texture patchers. Call after patchMeshes()
     *
     * @param shouldMultithread whether to patch on the task pool
     * @param progressCallback receives (completed, total) counts
     */
    static void patchTextures(const bool& shouldMultithread = true,
                              const std::function<void(size_t,
                                                       size_t)>& progressCallback = { });

    /**
     * @brief Returns a copy of the patch metadata collected since the last resetRunState()
     */
    static MeshPatchInfo patchMeta();

    /**
     * @brief Sorts shader matches so that the winning match comes first.
     *
     * Matches with a mod come before matches without one, higher mod priority first; ties are ordered by the shader
     * the match results in (better first), then by matched path.
     */
    static void sortMatches(std::vector<PatcherUtil::ShaderPatcherMatch>& matches);

    /**
     * @brief Sorts conflict viewer matches according to a mod priority list (first entry wins).
     *
     * Ties and mods not in the list are ordered by the shader the match results in (better first), then by matched
     * path.
     */
    static void sortMatches(std::vector<MatchMeta>& matches,
                            const std::vector<std::shared_ptr<PGModManager::Mod>>& modPriorityList);

    /**
     * @brief Check whether any mesh patch metadata has been collected.
     *
     * @return true if patch metadata exists
     * @return false if no patch metadata exists
     */
    static bool hasConflictData();

    /**
     * @brief Reset transient data collected during patching.
     *
     * Clears in-memory conflict metadata and diff JSON so subsequent patch runs
     * start from a clean state.
     */
    static void resetRunState();

    /**
     * @brief Deletes the files and folders PGPatcher generates from the output directory.
     *
     * Nothing is deleted, and a critical error is logged, when the directory holds anything PGPatcher did not create
     * (meta.ini is tolerated).
     *
     * @param preOutput when true, the previous output zip is deleted as well
     * @param shouldKeepIncrementalOutput when true, the meshes, textures and calientetools folders and the update cache
     * are kept so the previous output can be updated incrementally
     */
    static void deleteOutputDir(const bool& preOutput = true,
                                const bool& shouldKeepIncrementalOutput = false);

    /**
     * @brief Checks whether the output directory holds no files other than meta.ini and the update cache
     *
     * @return true if the output directory is missing or empty in that sense
     */
    static bool isOutputEmpty();

    /**
     * @brief Returns the CRC diff of the base meshes that changed: original and patched CRC32 per mesh path
     */
    static nlohmann::json diffJSON();

    /**
     * @brief Computes the digest of the shader matches a shape with the given texture slots would receive, using the
     * registered shader patchers and the current load order. Used by incremental runs to detect changed matches
     * without loading the mesh.
     *
     * @param nifPath relative path of the NIF the shape belongs to
     * @param slots texture slots of the shape
     * @param singlepassMATO whether the plugin use is a single pass material object
     * @param modelRecordType plugin record type of the use
     * @return uint64_t digest of the ordered match list
     */
    static uint64_t computeMatchesDigest(const std::filesystem::path& nifPath,
                                         const PGTypes::TextureSet& slots,
                                         bool isSinglepassMATO,
                                         const PGPlugin::ModelRecordType& modelRecordType);

private:
    // NIF Runners.

    /**
     * @brief Patches one mesh for each of its plugin uses, saves the outputs and records them for the update cache
     *
     * A mesh is skipped (Success) when its mod has meshes ignored, when it is a facegen mesh and those are excluded, or
     * when nothing uses it and forceBasePatch is off. Facegen and BodySlide ShapeData meshes have no plugin uses and
     * are patched for a dummy use.
     *
     * @param nifPath lower case relative path to the NIF file
     * @param setModelUsesQueue queue the plugin updates are posted to
     * @return Result::Failure if the mesh could not be processed
     */
    static TaskTracker::Result patchNIF(const std::filesystem::path& nifPath,
                                        TaskQueue& setModelUsesQueue,
                                        const bool& forceBasePatch = false,
                                        const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes = { },
                                        const bool& checkAllowedRecTypes = false,
                                        const bool& excludeFacegens = false);

    /**
     * @brief Replays the side effects of patching a NIF whose output from the previous run is still valid
     *
     * @param nifPath relative path to the NIF file
     * @param replayedMeshResults collects the mesh results of all replayed NIFs so their plugin model uses can be
     * applied in a single batch
     * @param replayedMeshResultsMutex mutex protecting replayedMeshResults
     * @return TaskTracker::Result result of the replay
     */
    static TaskTracker::Result replayNIF(const std::filesystem::path& nifPath,
                                         std::vector<PGMeshPermutationTracker::MeshResult>& replayedMeshResults,
                                         std::mutex& replayedMeshResultsMutex);

    /**
     * @brief Computes the digest of an ordered match list (see computeMatchesDigest)
     */
    static uint64_t digestMatches(const std::vector<PatcherUtil::ShaderPatcherMatch>& matches,
                                  const PatcherUtil::PatcherMeshObjectSet& patchers);

public:
    // NIF Helpers (the per-mesh pipeline).

    /**
     * @brief Whether a plugin use keeps the original, unpatched mesh instead of being patched: an ignored use, or a
     * use of a record type outside the allowed set when that set is enforced. The offline runner and the PGLite plan
     * apply the same rule.
     *
     * @param use the use's attributes
     * @param allowedModelRecTypes the record types that are patched
     * @param checkAllowedRecTypes whether the record type filter applies
     */
    [[nodiscard]] static bool isUseUnpatched(const PGPlugin::MeshUseAttributes& use,
                                             const std::unordered_set<PGPlugin::ModelRecordType>& allowedModelRecTypes,
                                             bool checkAllowedRecTypes);

    /**
     * @brief Patches a staged mesh for one plugin use: every patchable shape through processNIFShape(), then the
     * global patchers
     *
     * @param nifPath lower case relative path to the NIF file
     * @param nif staged mesh to patch in place
     * @param[in,out] meshMeta patch metadata of the mesh, extended with this use
     * @param meshUse the plugin use being patched
     * @param isSinglepassMATO whether the use is a single pass material object
     * @param formKey the plugin use being patched
     * @param modelRecordType plugin record type of the use
     * @param[in,out] alternateTextures alternate textures of the use by 3D shape index, patched in place
     * @param[out] nonAltTexShapes 3D indices of the shapes that had no alternate texture (skin tint shapes always
     * count as such, their alternate textures are ignored like the engine does)
     * @return false if a shape could not be processed
     */
    static bool processNIF(const std::filesystem::path& nifPath,
                           nifly::NifFile* nif,
                           MeshMeta& meshMeta,
                           const PGPlugin::MeshUseAttributes& meshUse,
                           bool isSinglepassMATO,
                           const PGMeshPermutationTracker::FormKey& formKey,
                           const PGPlugin::ModelRecordType& modelRecordType,
                           std::unordered_map<unsigned,
                                              PGTypes::TextureSet>& alternateTextures,
                           std::unordered_set<unsigned>& nonAltTexShapes);

private:
    /**
     * @brief Patches one shape: a shader shared with other shapes is copied for it, then pre patchers, the winning
     * shader match (first enabled match of matches()) with its transform, post patchers, then the texture set is
     * written back
     *
     * @param nif mesh the shape belongs to
     * @param nifShape shape to patch
     * @param[in,out] meshShapeMeta patch metadata of the shape, extended with this use
     * @param patchers patcher objects created for this mesh and use
     * @param isSinglepassMATO whether the use is a single pass material object
     * @param formKey the plugin use being patched
     * @param modelRecordType plugin record type of the use
     * @param[in,out] alternateTexture the use's alternate texture for this shape, patched in place instead of the
     * mesh's texture set; nullptr when the shape has none
     * @return true, a deleted shape ends processing early
     * @throws std::runtime_error if nif or nifShape is null
     */
    static bool processNIFShape(nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                MeshShapeMeta& meshShapeMeta,
                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                bool isSinglepassMATO,
                                const PGMeshPermutationTracker::FormKey& formKey,
                                const PGPlugin::ModelRecordType& modelRecordType,
                                PGTypes::TextureSet* alternateTexture = nullptr);

    /**
     * @brief Collects the shader matches of a texture set from every shader patcher, sorted with the winner first
     *
     * Matches whose file is not in the load order are dropped and the conflicts between the matching mods are recorded
     * in the mod manager. The digest of the full sorted list is recorded for the update cache. With patcherObjects and
     * shape given, matches the shape cannot take (canApply()) are dropped unless a transform makes them applicable, in
     * which case shaderTransformTo is set.
     *
     * @param slots texture set to match
     * @param patchers shader patchers to ask
     * @param isSinglepassMATO whether the use is a single pass material object
     * @param modelRecordType plugin record type of the use
     * @param patcherObjects patchers with a loaded mesh, for the canApply() checks. Optional, must come with shape
     * @param shape shape the matches are for. Optional, must come with patcherObjects
     * @throws std::runtime_error if only one of patcherObjects and shape is given or their patcher sets differ in size
     */
    static std::vector<PatcherUtil::ShaderPatcherMatch> matches(const PGTypes::TextureSet& slots,
                                                                const PatcherUtil::PatcherMeshObjectSet& patchers,
                                                                bool isSinglepassMATO,
                                                                const PGPlugin::ModelRecordType& modelRecordType,
                                                                const PatcherUtil::PatcherMeshObjectSet* patcherObjects
                                                                = nullptr,
                                                                nifly::NiShape* shape = nullptr);

    /**
     * @brief Applies the transform of a match in place when it has one, so that the match then refers to the target
     * shader
     *
     * @param[in,out] match match to transform
     * @param patchers patcher set holding the transform
     * @return true if a transform was applied
     */
    static bool applyTransformIfNeeded(PatcherUtil::ShaderPatcherMatch& match,
                                       const PatcherUtil::PatcherMeshObjectSet& patchers);

    /**
     * @brief Instantiates every registered mesh patcher for a mesh and plugin use
     */
    static PatcherUtil::PatcherMeshObjectSet createNIFPatcherObjects(const std::filesystem::path& nifPath,
                                                                     nifly::NifFile* nif,
                                                                     const PGPlugin::MeshUseAttributes& meshUse);

private:
    // DDS Runners.
    /**
     * @brief Patches one texture: runs the texture hooks registered for it, then the global texture patchers, and
     * writes the texture to the output when a global patcher changed it
     *
     * @param ddsPath lower case relative path to the DDS file
     * @return Result::Failure if the texture could not be read, converted or written
     * @throws std::runtime_error if the path does not end in .dds
     */
    static TaskTracker::Result patchDDS(const std::filesystem::path& ddsPath);

    /**
     * @brief Instantiates every registered texture patcher for a texture
     */
    static PatcherUtil::PatcherTextureObjectSet createDDSPatcherObjects(const std::filesystem::path& ddsPath,
                                                                        DirectX::ScratchImage* dds);
};
