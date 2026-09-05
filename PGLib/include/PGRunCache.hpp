#pragma once

#include "PGDirectory.hpp"
#include "PGPatcher.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"

#include <DirectXTex.h>
#include <spdlog/common.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Incremental ("update") cache for PGPatcher output directories.
 *
 * When the output directory already contains a previous PGPatcher output (and zip output is disabled), a binary cache
 * file written at the end of the previous run describes, for every patched mesh, every input that patch depended on:
 * the identity (source, size, modification time) of files that were read, the results of file existence / texture
 * type / texture attribute / texture map lookups, which mod owned each queried file, and a digest of the match list
 * computed for every shape. It also stores everything needed to replay the side effects of patching a mesh (plugin
 * model uses, light placer edits, diff CRCs, conflict viewer metadata, texture hook registrations and log messages).
 *
 * On the next run every recorded dependency is re-evaluated against the current load order without reading any NIF.
 * Meshes whose dependencies all still hold keep their previous output files and have their side effects replayed;
 * every other mesh is patched normally after its stale outputs are deleted. The classification data derived from
 * reading meshes and textures (texture slot votes, complex material classification, DDS metadata) is cached as well
 * so unchanged files are never read again.
 *
 * All recording is done through a thread-local recorder that is active while a single mesh is being patched, so
 * patchers do not need to know about the cache: any query they make through PGDirectory, PGModManager or
 * PGNIFUtil::getTexMatch is recorded automatically.
 */
class PGRunCache {
public:
    static inline const std::filesystem::path CACHE_FILENAME = L"PGPatcher_UpdateCache.bin";

    /// @brief Texture hook patchers whose generated outputs are tracked by the cache
    enum class HookKind : uint8_t { CONVERT_TO_CM, FIX_SSS };

    /// @brief Result of a file existence query as seen by a patcher
    enum class FileExistsState : uint8_t { MISSING, EXISTS, GENERATED };

    using TextureVote = PGTypes::TextureVote;
    using CMClassification = PGTypes::CMClassification;

    using MeshUses = std::vector<std::pair<PGMeshPermutationTracker::FormKey, PGPlugin::MeshUseAttributes>>;

    struct MatchMetaRecord {
        std::wstring modName; /**< Empty if the match had no owning mod */
        PGEnums::ShapeShader shader {};
        PGEnums::ShapeShader shaderTransformTo {};
        std::filesystem::path matchedPath;
        std::vector<std::pair<PGEnums::TextureSlots, std::wstring>> resultTextureMods;
    };

    struct MeshShapeMetaRecord {
        uint32_t blockID = 0;
        std::string shapeName;
        std::vector<std::string> prePatchersApplied;
        std::vector<std::string> postPatchersApplied;
        std::vector<std::pair<PGMeshPermutationTracker::FormKey, std::vector<MatchMetaRecord>>> matches;
    };

    struct MeshMetaRecord {
        std::vector<std::string> globalPatchersApplied;
        std::vector<PGMeshPermutationTracker::FormKey> formKeys;
        std::vector<std::pair<size_t, MeshShapeMetaRecord>> shapeMeta;
    };

    struct MatchesDep {
        PGTypes::TextureSet slots;
        bool singlepassMATO = false;
        PGPlugin::ModelRecordType recType = PGPlugin::ModelRecordType::UNKNOWN;
        uint64_t digest = 0;
    };

    struct Message {
        spdlog::level::level_enum level = spdlog::level::warn;
        std::wstring text;
    };

    /**
     * @brief Everything recorded about one patched mesh: its inputs (dependencies) and its outputs / side effects
     */
    struct MeshRecord {
        MeshUses uses; /**< Plugin uses of the mesh as they were when it was patched */

        // Dependencies
        std::vector<std::pair<std::wstring, BethesdaDirectory::FileIdentity>> fileIdentityDeps;
        std::vector<std::pair<std::wstring, FileExistsState>> fileExistsDeps;
        std::vector<std::pair<std::wstring, PGEnums::TextureType>> textureTypeDeps;
        std::vector<std::tuple<std::wstring, PGEnums::TextureAttribute, bool>> textureAttributeDeps;
        std::vector<std::pair<std::wstring, uint8_t>> textureAttributesDeps; /**< Attribute set as bitmask */
        std::vector<std::tuple<std::wstring, PGEnums::TextureType, uint64_t>> texMatchDeps;
        std::vector<std::pair<std::wstring, std::wstring>> modOfFileDeps;
        std::vector<std::tuple<std::wstring, bool, bool>> modStateDeps; /**< mod name -> (enabled, meshes ignored) */
        std::vector<MatchesDep> matchesDeps;

        // Outputs and side effects
        std::vector<std::pair<std::wstring, uint64_t>> outputFiles; /**< Relative output path -> size in bytes */
        std::vector<PGMeshPermutationTracker::MeshResult> meshResults;
        bool hasDiff = false;
        uint64_t crc32Original = 0;
        uint64_t crc32Patched = 0;
        MeshMetaRecord meta;
        std::vector<std::pair<HookKind, std::wstring>> hookRegistrations;
        std::vector<Message> messages;
    };

    /**
     * @brief A texture generated by a texture hook patcher
     */
    struct HookOutputRecord {
        HookKind kind = HookKind::CONVERT_TO_CM;
        std::wstring source; /**< Relative path of the texture the hook was applied to */
        BethesdaDirectory::FileIdentity sourceIdentity;
        std::wstring output; /**< Relative path of the generated texture */
        uint64_t size = 0; /**< Size of the generated texture in bytes */
    };

    /**
     * @brief Cached knowledge about a texture in the load order
     */
    struct TextureInfo {
        BethesdaDirectory::FileIdentity identity;
        bool hasCM = false;
        CMClassification cm;
        bool hasMeta = false;
        DirectX::TexMetadata meta {};
    };

    struct MeshVotes {
        BethesdaDirectory::FileIdentity identity;
        std::vector<TextureVote> votes;
    };

    /**
     * @brief Complete persisted state of one run
     */
    struct CacheData {
        std::string pgVersion;
        uint64_t configFingerprint = 0;
        uint64_t pluginFingerprint = 0;
        std::unordered_map<std::wstring, TextureInfo> textures;
        std::unordered_map<std::wstring, MeshVotes> meshVotes;
        std::unordered_map<std::wstring, MeshUses> meshUses;
        std::unordered_map<std::wstring, HookOutputRecord> hookOutputs; /**< Keyed by output path */
        std::unordered_map<std::wstring, MeshRecord> meshRecords;
    };

    /**
     * @brief RAII recorder that is active while a single mesh is patched on the current thread.
     *
     * Every dependency query made on this thread while the recorder is alive is recorded. Call commit() once the mesh
     * was patched successfully; a recorder that is destroyed without being committed discards its record.
     */
    class MeshRecorder : public BethesdaDirectory::FileQueryObserver {
    private:
        std::filesystem::path m_nifPath;
        MeshRecord m_record;
        bool m_committed = false;
        bool m_valid = true;

        // Dedup helpers
        std::unordered_map<std::wstring, BethesdaDirectory::FileIdentity> m_identityDeps;
        std::unordered_set<std::wstring> m_existsDeps;
        std::unordered_map<std::wstring, PGEnums::TextureType> m_textureTypeDeps;
        std::unordered_set<std::wstring> m_textureAttributeDeps;
        std::unordered_map<std::wstring, uint8_t> m_textureAttributesDeps;
        std::unordered_set<std::wstring> m_texMatchDeps;
        std::unordered_map<std::wstring, std::wstring> m_modOfFileDeps;
        std::unordered_map<std::wstring, std::pair<bool, bool>> m_modStateDeps;
        std::unordered_set<std::wstring> m_matchesDeps;
        std::unordered_set<std::wstring> m_hookRegistrations;

    public:
        explicit MeshRecorder(std::filesystem::path nifPath);
        ~MeshRecorder() override;
        MeshRecorder(const MeshRecorder&) = delete;
        auto operator=(const MeshRecorder&) -> MeshRecorder& = delete;
        MeshRecorder(MeshRecorder&&) = delete;
        auto operator=(MeshRecorder&&) -> MeshRecorder& = delete;

        void onIsFile(const std::filesystem::path& relPath,
                      bool exists,
                      bool generated) override;
        void onGetFile(const std::filesystem::path& relPath,
                       const BethesdaDirectory::FileIdentity& identity) override;

        void recordTextureType(const std::filesystem::path& path,
                               const PGEnums::TextureType& type);
        void recordTextureAttribute(const std::filesystem::path& path,
                                    const PGEnums::TextureAttribute& attribute,
                                    bool value);
        void recordTextureAttributes(const std::filesystem::path& path,
                                     const std::unordered_set<PGEnums::TextureAttribute>& attributes);
        void recordTexMatch(const std::wstring& base,
                            const PGEnums::TextureType& type,
                            const std::vector<PGTypes::PGTexture>& results);
        void recordModOfFile(const std::filesystem::path& path,
                             const std::wstring& modName);
        void recordModState(const std::wstring& modName,
                            bool isEnabled,
                            bool areMeshesIgnored);
        void recordMatches(const PGTypes::TextureSet& slots,
                           bool singlepassMATO,
                           const PGPlugin::ModelRecordType& recType,
                           uint64_t digest);
        void recordHookRegistration(const HookKind& kind,
                                    const std::filesystem::path& texPath);
        void recordOutputFile(const std::filesystem::path& relPath,
                              uint64_t size);
        void recordMessage(const spdlog::level::level_enum& level,
                           const std::wstring& message);

        void setUses(const MeshUses& uses);
        void setMeshResults(const std::vector<PGMeshPermutationTracker::MeshResult>& results);
        void setDiff(uint64_t crc32Original,
                     uint64_t crc32Patched);
        void setMeta(const PGPatcher::MeshMeta& meta);

        /**
         * @brief Stores the record in the current run. Must be called once the mesh has been fully patched.
         */
        void commit();
    };

    /**
     * @brief RAII guard that suspends dependency recording on the current thread while alive.
     *
     * Used around computations whose result is recorded as a whole (the match digest) and around reads whose content
     * does not influence mesh output (DDS pixel data).
     */
    class SuspendRecording {
    public:
        SuspendRecording();
        ~SuspendRecording();
        SuspendRecording(const SuspendRecording&) = delete;
        auto operator=(const SuspendRecording&) -> SuspendRecording& = delete;
        SuspendRecording(SuspendRecording&&) = delete;
        auto operator=(SuspendRecording&&) -> SuspendRecording& = delete;
    };

private:
    static constexpr uint32_t FORMAT_MAGIC = 0x43524750; // "PGRC"
    static constexpr uint32_t FORMAT_VERSION = 1;

    static inline bool s_enabled = false;
    static inline std::filesystem::path s_cacheFile;
    static inline std::unique_ptr<CacheData> s_previous;

    // Session state (survives in-process re-runs of the patching step)
    static inline uint64_t s_configFingerprint = 0;
    static inline uint64_t s_pluginFingerprint = 0;
    static inline std::shared_mutex s_sessionMutex;
    static inline std::unordered_map<std::wstring, MeshVotes> s_sessionVotes;
    static inline std::unordered_map<std::wstring, std::pair<BethesdaDirectory::FileIdentity, CMClassification>>
        s_sessionCM;

    // Current run state
    static inline std::mutex s_runMutex;
    static inline std::unordered_map<std::wstring, MeshRecord> s_currentRecords;
    static inline std::unordered_map<std::wstring, HookOutputRecord> s_currentHookOutputs;
    static inline std::vector<std::pair<HookKind, std::wstring>> s_reusedHooks;
    static inline std::unordered_map<std::wstring, uint64_t> s_outputSnapshot; /**< lowercase rel path -> size */
    static inline std::unordered_set<std::wstring> s_protectedOutputs;
    static inline std::unordered_set<std::wstring> s_skippable;

    // Thread-local recording state
    static inline thread_local MeshRecorder* s_activeRecorder = nullptr;
    static inline thread_local int s_suspendDepth = 0;

    static void captureLogMessage(spdlog::level::level_enum level,
                                  const std::wstring& message);

    static auto pathKey(const std::filesystem::path& path) -> std::wstring;
    static auto pathKey(const std::wstring& path) -> std::wstring;

    static auto outputRoot() -> std::filesystem::path;

    static auto hookOutputPath(const HookKind& kind,
                               const std::filesystem::path& texPath) -> std::filesystem::path;

    static auto evaluateMesh(const std::filesystem::path& nifPath,
                             const PGDirectory::NifCache& nifCache,
                             const CacheData& previous) -> bool;

    static auto hashTexMatchResults(const std::vector<PGTypes::PGTexture>& results) -> uint64_t;

    static auto attributesToMask(const std::unordered_set<PGEnums::TextureAttribute>& attributes) -> uint8_t;

    static auto loadFromFile(const std::filesystem::path& cacheFile) -> std::unique_ptr<CacheData>;
    static auto saveToFile(const std::filesystem::path& cacheFile,
                           const CacheData& data) -> bool;

    static void removeEmptyDirectories(const std::filesystem::path& root);

public:
    /**
     * @brief Checks cheaply whether an output directory holds a previous output that this version can update (a cache
     * file with a compatible header). Only the file header is read, so this can be called from UI event handlers.
     *
     * @param outputDir Output directory to check.
     * @return true if an update of the previous output is possible.
     */
    static auto isUpdateAvailable(const std::filesystem::path& outputDir) -> bool;

    /**
     * @brief Sets up the cache for this process.
     *
     * @param cacheFile Path of the cache file inside the output directory.
     * @param enabled Whether the cache system is active at all (false when zip output is enabled).
     * @param ignoreExisting When true an existing cache file is ignored: the output is regenerated from scratch (a new
     * cache is still written at the end of the run).
     */
    static void initialize(const std::filesystem::path& cacheFile,
                           bool enabled,
                           bool ignoreExisting = false);

    /**
     * @brief Whether the cache system is active (a cache file will be written at the end of the run).
     */
    static auto isEnabled() -> bool;

    /**
     * @brief Whether a previous run is available to update from (a valid cache was loaded or a run completed).
     */
    static auto hasPreviousRun() -> bool;

    /**
     * @brief Whether the patch records of the previous run are usable (config fingerprint matches).
     */
    static auto arePreviousRecordsValid() -> bool;

    /**
     * @brief Whether the plugin mesh uses of the previous run are usable (plugin fingerprint matches). Requires
     * setPluginFingerprint() to have been called.
     */
    static auto arePreviousMeshUsesValid() -> bool;

    /**
     * @brief Sets the fingerprint of everything in the run configuration that influences mesh output.
     */
    static void setConfigFingerprint(uint64_t fingerprint);

    /**
     * @brief Sets the fingerprint of the active plugin load order (plugin names, sizes and modification times).
     */
    static void setPluginFingerprint(uint64_t fingerprint);

    /**
     * @brief Pre-seeds the PGD3D DDS metadata cache with cached metadata of textures whose identity did not change.
     * Requires the file map to be populated.
     */
    static void seedTextureMetadata();

    /**
     * @brief Marks a relative output path that must never be deleted by output pruning (e.g. deployed assets).
     */
    static void addProtectedOutput(const std::filesystem::path& relPath);

    /**
     * @brief Resets per-run state (including protected outputs). Call at the start of every patching step.
     */
    static void beginRun();

    /**
     * @brief Records what is currently in the output directory (meshes and textures folders).
     */
    static void snapshotOutputDirectory();

    /**
     * @brief Assembles the state of the finished run, optionally writes it to disk, and makes it the previous run.
     *
     * @param save Whether to write the cache file.
     * @return true on success (or when the cache is disabled).
     */
    static auto finishRun(bool save) -> bool;

    /**
     * @brief Deletes the cache file and forgets the previous run.
     */
    static void discard();

    //
    // Classification caches (used by PGDirectory::mapFiles)
    //

    static auto tryGetCachedMeshVotes(const std::filesystem::path& nifPath,
                                      const BethesdaDirectory::FileIdentity& identity,
                                      std::vector<TextureVote>& votes) -> bool;
    static void storeMeshVotes(const std::filesystem::path& nifPath,
                               const BethesdaDirectory::FileIdentity& identity,
                               const std::vector<TextureVote>& votes);
    static auto tryGetCachedMeshUses(const std::filesystem::path& nifPath,
                                     MeshUses& uses) -> bool;
    static auto tryGetCachedCMClassification(const std::filesystem::path& texture,
                                             const BethesdaDirectory::FileIdentity& identity,
                                             CMClassification& result) -> bool;
    static void storeCMClassification(const std::filesystem::path& texture,
                                      const BethesdaDirectory::FileIdentity& identity,
                                      const CMClassification& result);

    //
    // Recording (no-ops unless a MeshRecorder is active on the calling thread)
    //

    static auto isRecording() -> bool;
    static void recordTextureType(const std::filesystem::path& path,
                                  const PGEnums::TextureType& type);
    static void recordTextureAttribute(const std::filesystem::path& path,
                                       const PGEnums::TextureAttribute& attribute,
                                       bool value);
    static void recordTextureAttributes(const std::filesystem::path& path,
                                        const std::unordered_set<PGEnums::TextureAttribute>& attributes);
    static void recordTexMatch(const std::wstring& base,
                               const PGEnums::TextureType& type,
                               const std::vector<PGTypes::PGTexture>& results);
    static void recordModOfFile(const std::filesystem::path& path,
                                const std::wstring& modName);
    static void recordModState(const std::wstring& modName,
                               bool isEnabled,
                               bool areMeshesIgnored);
    static void recordMatches(const PGTypes::TextureSet& slots,
                              bool singlepassMATO,
                              const PGPlugin::ModelRecordType& recType,
                              uint64_t digest);
    static void recordHookRegistration(const HookKind& kind,
                                       const std::filesystem::path& texPath);
    static void recordOutputFile(const std::filesystem::path& relPath,
                                 uint64_t size);

    /**
     * @brief Records a texture generated by a hook patcher (global, called from the texture phase).
     */
    static void recordHookOutput(const HookKind& kind,
                                 const std::filesystem::path& source,
                                 const std::filesystem::path& output);

    /**
     * @brief Attaches a message emitted outside of mesh patching (e.g. weighted variant validation) to a mesh record.
     */
    static void appendDeferredMessage(const std::filesystem::path& nifPath,
                                      const spdlog::level::level_enum& level,
                                      const std::wstring& message);

    //
    // Evaluation and replay
    //

    /**
     * @brief Determines which meshes can keep their previous output.
     *
     * @param meshes All meshes of the current run with their plugin uses.
     * @param multiThread Whether to evaluate in parallel.
     * @param progressCallback Optional progress callback.
     * @return Set of mesh paths whose previous output is still valid.
     */
    static auto evaluateMeshes(const std::unordered_map<std::filesystem::path,
                                                        PGDirectory::NifCache>& meshes,
                               bool multiThread,
                               const std::function<void(size_t,
                                                        size_t)>& progressCallback
                               = {}) -> std::unordered_set<std::filesystem::path>;

    /**
     * @brief Deletes every file in the output meshes/textures folders that is not an output of a skippable mesh, a
     * previously generated hook texture, or a protected output. Must be called after evaluateMeshes().
     */
    static void pruneStaleOutputs(const std::unordered_set<std::filesystem::path>& skippable);

    /**
     * @brief Returns the previous record of a mesh, or nullptr.
     */
    static auto getPreviousRecord(const std::filesystem::path& nifPath) -> const MeshRecord*;

    /**
     * @brief Copies the previous record of a skipped mesh into the current run.
     */
    static void carryOverRecord(const std::filesystem::path& nifPath);

    /**
     * @brief Replays a texture hook registration of a skipped mesh: reuses the previous output if it is still valid,
     * otherwise schedules regeneration.
     */
    static void replayHookRegistration(const HookKind& kind,
                                       const std::filesystem::path& texPath);

    /**
     * @brief Reuses the previously generated output of a texture hook when the source texture did not change and the
     * output is still present in the output directory. Called by the hook patchers before scheduling generation.
     *
     * @return true if the previous output was reused (no generation required).
     */
    static auto tryReuseHookOutput(const HookKind& kind,
                                   const std::filesystem::path& texPath) -> bool;

    /**
     * @brief Finalizes texture hooks before the texture phase: replays reused outputs and deletes stale ones.
     */
    static void finalizeHooks();

    /**
     * @brief Rebuilds conflict viewer metadata from a record (mods are resolved by name).
     */
    static auto buildMeshMeta(const MeshMetaRecord& record) -> PGPatcher::MeshMeta;

    /**
     * @brief Replays the log messages stored in a record through the logger.
     */
    static void replayMessages(const MeshRecord& record);
};
