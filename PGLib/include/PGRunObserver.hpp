#pragma once

#include "PGPlugin.hpp"
#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Observer of a patch run: the lookups the mesh pipeline makes and the classifications the file map computes.
 *
 * The offline update cache (PGRunCache) implements it to record what a mesh depended on and to replay classifications
 * of a previous run. Every function is a no-op or a cache miss by default, and the shared code reports through
 * current(), so a program without an update cache (the PGLite runtime) does not compile PGRunCache at all.
 */
class PGRunObserver {
public:
    /// @brief Plugin uses of a mesh, as PGPlugin::modelUses() returns them.
    using MeshUses = std::vector<std::pair<PGMeshPermutationTracker::FormKey, PGPlugin::MeshUseAttributes>>;

    /// @brief The texture hook patchers, whose generated textures an observer tracks across runs (the offline update
    /// cache). The PGLite runtime generates them through the hooks' generate() and keys its texture cache by the kind.
    enum class HookKind : uint8_t { ConvertToCM, FixSSS };

    PGRunObserver() = default;
    virtual ~PGRunObserver() = default;
    PGRunObserver(const PGRunObserver&) = default;
    PGRunObserver& operator=(const PGRunObserver&) = default;
    PGRunObserver(PGRunObserver&&) = default;
    PGRunObserver& operator=(PGRunObserver&&) = default;

    //
    // Dependencies of the mesh being patched on the calling thread.
    //

    /// @brief The type a texture type lookup returned.
    virtual void recordTextureType(const std::filesystem::path& path,
                                   const PGEnums::TextureType& type);

    /// @brief The result of a texture attribute lookup.
    virtual void recordTextureAttribute(const std::filesystem::path& path,
                                        const PGEnums::TextureAttribute& attribute,
                                        bool hasAttribute);

    /// @brief The attributes a texture attributes lookup returned.
    virtual void recordTextureAttributes(const std::filesystem::path& path,
                                         const std::unordered_set<PGEnums::TextureAttribute>& attributes);

    /// @brief The textures a texture map lookup returned, see PGNIFUtil::texMatch().
    virtual void recordTexMatch(const std::wstring& base,
                                const PGEnums::TextureType& type,
                                const std::vector<PGTypes::PGTexture>& results);

    /// @brief The mod a file was resolved to (empty for none).
    virtual void recordModOfFile(const std::filesystem::path& path,
                                 const std::wstring& modName);

    /// @brief The digest of the shader matches computed for a texture set.
    virtual void recordMatches(const PGTypes::TextureSet& slots,
                               bool isSinglepassMATO,
                               const PGPlugin::ModelRecordType& recType,
                               uint64_t digest);

    /// @brief An output mesh written for the mesh being patched.
    virtual void recordOutputFile(const std::filesystem::path& relPath,
                                  uint64_t size);

    /// @brief Stops recording on the calling thread until the matching resumeRecording(). Calls nest.
    virtual void suspendRecording();

    /// @brief Resumes recording on the calling thread, see suspendRecording().
    virtual void resumeRecording();

    //
    // Texture hooks (PatcherTextureHook): the textures the mesh patchers register and the textures made from them.
    //

    /// @brief A texture a mesh patcher registered with a texture hook. Reported even while recording is suspended: a
    /// registration is a side effect of the patch, not a query.
    virtual void recordHookRegistration(const HookKind& kind,
                                        const std::filesystem::path& texPath);

    /**
     * @brief Asks whether the texture a hook generated from this source in a previous run still serves, in which case
     * the hook generates nothing. The default answer is no.
     */
    virtual bool tryReuseHookOutput(const HookKind& kind,
                                    const std::filesystem::path& texPath);

    /// @brief A texture a hook generated and wrote to the output folder.
    virtual void recordHookOutput(const HookKind& kind,
                                  const std::filesystem::path& source,
                                  const std::filesystem::path& output);

    //
    // Reads that failed.
    //

    /**
     * @brief A texture file a lookup needed and could not read: the file could not be opened or read, as opposed to
     * not being a DDS file. The lookup treats the texture as missing. Reported even while recording is suspended, as
     * it is an event of the run, not a query; the PGLite runtime patches the mesh again at a later open because of it.
     */
    virtual void recordTextureReadFailure(const std::filesystem::path& path);

    //
    // Classifications of a previous run.
    //

    /// @brief Returns the texture votes of a mesh with the same identity from a previous run, if any.
    virtual bool tryGetCachedMeshVotes(const std::filesystem::path& nifPath,
                                       const BethesdaDirectory::FileIdentity& identity,
                                       std::vector<PGTypes::TextureVote>& votes);

    /// @brief Keeps the texture votes of a mesh read in this run.
    virtual void storeMeshVotes(const std::filesystem::path& nifPath,
                                const BethesdaDirectory::FileIdentity& identity,
                                const std::vector<PGTypes::TextureVote>& votes);

    /// @brief Returns the plugin uses of a mesh from a previous run, if they are still valid.
    virtual bool tryGetCachedMeshUses(const std::filesystem::path& nifPath,
                                      MeshUses& uses);

    /// @brief Returns the complex material classification of a texture with the same identity from a previous run.
    virtual bool tryGetCachedCMClassification(const std::filesystem::path& texture,
                                              const BethesdaDirectory::FileIdentity& identity,
                                              PGTypes::CMClassification& result);

    /// @brief Keeps the complex material classification of a texture made in this run.
    virtual void storeCMClassification(const std::filesystem::path& texture,
                                       const BethesdaDirectory::FileIdentity& identity,
                                       const PGTypes::CMClassification& result);

    //
    // The observer of the process.
    //

    /**
     * @brief Returns the installed observer, or the default one (which records nothing) when none is installed.
     */
    static PGRunObserver& current();

    /**
     * @brief Installs the observer the shared code reports to. nullptr restores the default one. The observer must
     * outlive its installation.
     */
    static void install(PGRunObserver* observer);

    /**
     * @brief RAII guard that suspends recording on the current thread while alive.
     *
     * Used around computations whose result is recorded as a whole (the match digest) and around reads whose content
     * does not influence mesh output (DDS pixel data).
     */
    class SuspendRecording {
    public:
        SuspendRecording();
        ~SuspendRecording();
        SuspendRecording(const SuspendRecording&) = delete;
        SuspendRecording& operator=(const SuspendRecording&) = delete;
        SuspendRecording(SuspendRecording&&) = delete;
        SuspendRecording& operator=(SuspendRecording&&) = delete;

    private:
        /// @brief The observer that was suspended, so an install in between does not unbalance another one.
        PGRunObserver* m_observer;
    };

private:
    static inline std::atomic<PGRunObserver*> s_installed { nullptr };
};
