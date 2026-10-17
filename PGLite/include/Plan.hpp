#pragma once

#include "PGParams.hpp"
#include "PGPlugin.hpp"
#include "PatchStream.hpp"
#include "RecordEdits.hpp"
#include "Uses.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PGLite {

/**
 * @brief The per-use inputs that can change a patch result (DESIGN.md section 4.4). Uses with equal contexts always
 * patch identically.
 */
struct Context {
    /// @brief Alternate textures by 3D index, lower case with the textures\ prefix.
    std::unordered_map<unsigned, PGTypes::TextureSet> alternateTextures;
    bool isSinglepassMATO = false;
    bool isGrass = false;

    bool operator==(const Context& other) const = default;
};

/**
 * @brief One original mesh with all its uses, or both files of a weighted pair (DESIGN.md sections 4.3 and 4.4).
 *
 * The plan of a family is computed from the records at data load: its members are its distinct contexts, each with
 * the registered path per file (the original path for the primary context, a variant path for the others). The patch
 * of a family runs when the first of its streams opens, for every member at once, and applies the record edits the
 * results determine. PGLite keeps the outcome of that patch, never the bytes: a later open of another member patches
 * that member's group again.
 */
class Family {
public:
    /// @brief A use of the family, with the file of the family its record names.
    struct UseRef {
        const RecordUse* use = nullptr;
        std::size_t pathIndex = 0;
    };

    /// @brief One context of the family.
    struct Member {
        Context context;
        /// @brief The attributes the patchers see: those of the first use of the context.
        PGPlugin::MeshUseAttributes attributes;
        /// @brief The first use of the context, for the log.
        PGMeshPermutationTracker::FormKey formKey;
        std::vector<UseRef> uses;
        /// @brief The registered path per file of the family.
        std::vector<std::filesystem::path> paths;
        /// @brief 0 for the original path.
        unsigned variantNumber = 0;
    };

    /// @brief How the patch of a family ended.
    enum class State : uint8_t { Unpatched, Patched, Failed, IgnoredByMod };

    /// @brief The outcome of a member's patch for one file of the family.
    struct MemberOutput {
        /// @brief The result equals the original mesh, which the member keeps.
        bool isOriginalKept = false;
        /// @brief Index of the tracker output the result went to.
        std::size_t output = 0;
        std::unordered_map<int, int> idxCorrections;
        std::vector<PGMeshPermutationTracker::BlockKind> blockKinds;
        /// @brief The member's alternate textures after the patch, by original 3D index.
        std::unordered_map<unsigned, PGTypes::TextureSet> altTexResults;
    };

    Family() = default;
    ~Family() = default;
    Family(const Family&) = delete;
    Family& operator=(const Family&) = delete;
    Family(Family&&) = delete;
    Family& operator=(Family&&) = delete;

    /// @brief The files of the family: one path, or the _0 and _1 paths of a weighted pair.
    std::vector<std::filesystem::path> basePaths;
    /// @brief The engine's own streams for the files, set when the streams are registered; a file the engine cannot
    /// resolve has none, and nothing is registered or patched for it. Empty until the family is registered.
    std::vector<std::shared_ptr<SourceStream>> sources;
    std::vector<Member> members;
    /// @brief Uses that keep the original, unpatched mesh: filtered by the record type setting or ignored.
    std::vector<UseRef> unpatchedUses;
    /// @brief Whether the original path serves the unpatched mesh, so every member has a variant path.
    bool isBaseUnpatched = false;
    /// @brief A facegen head: one dummy member without record uses.
    bool isFacegen = false;

    /**
     * @brief The bytes the stream of a member serves: patches the family on its first open, then produces the member's
     * group. Never throws. A first patch that could not read one of its inputs (EngineReadError: the engine's stream
     * of the mesh, a texture header, a complex material JSON) is not kept: every open of the family inside the
     * readFailureRetryInterval that follows serves the original mesh without patching, so the opens of one burst
     * stay consistent with each other, and the first open after the interval patches the family again. Up to
     * maxPatchReadFailures such attempts may fail; after those the family behaves as it did before the retries: a
     * texture header that cannot be read is treated as missing and the patch is kept, and a mesh or JSON that cannot
     * be read leaves the family unpatched for the session. Any other failure of the first patch leaves the family
     * unpatched for the session at once. A failure of a later open serves the original bytes for that open only,
     * since the records already carry the edits of the first patch; a texture header that cannot be read then is
     * treated as missing, so the bytes keep the shape order the records were edited for. What the retries cannot
     * undo: the engine keeps an original it was served until its references let go, so a reference it builds from
     * that original after a later attempt edited the records gets the edits of the patched mesh.
     *
     * @param member Index of the member.
     * @param pathIndex Index of the file of the family.
     * @param textureSets Where patched alternate textures get their texture set objects.
     * @param generation The plan generation, which a deferred record edit checks before it runs.
     * @return The bytes, or nullptr if not even the original mesh can be read.
     */
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> bytes(std::size_t member,
                                                                      std::size_t pathIndex,
                                                                      TextureSetFactory& textureSets,
                                                                      unsigned generation);

    /**
     * @brief Counts a texture file a lookup of the patch running on the calling thread could not read, as the run
     * observer reports it: the patch is not kept, see bytes().
     */
    static void noteTextureReadFailure(const std::filesystem::path& texPath);

    [[nodiscard]] State state() const { return m_state; }

private:
    /// @brief How many attempts of the first patch may end in a read failure before the family stops retrying, see
    /// bytes().
    static constexpr unsigned maxPatchReadFailures = 3;

    /// @brief The bytes of bytes(), or throws. Under m_patchMutex.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> serve(std::size_t member,
                                                                      std::size_t pathIndex,
                                                                      TextureSetFactory& textureSets,
                                                                      unsigned generation);

    /// @brief The patch of every member of every file, then the record edits. Under m_patchMutex.
    void patch(std::size_t wantedMember,
               std::size_t wantedPath,
               TextureSetFactory& textureSets,
               unsigned generation,
               std::shared_ptr<const std::vector<std::byte>>& wantedBytes);

    /// @brief Patches one member of one file again and serializes it.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> produce(std::size_t member,
                                                                        std::size_t pathIndex);

    /// @brief Reads the original bytes of a file of the family through the engine's stream.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> original(std::size_t pathIndex);

    /// @brief Applies the index corrections and texture sets of the patch to every record of the family, through
    /// the runtime's snapshot, which keeps an entry another plugin set.
    void editRecords(TextureSetFactory& textureSets);

    /// @brief The index of a file with a source: the given one, or the resolved file of a pair the given one is
    /// missing from.
    [[nodiscard]] std::size_t resolvedPathIndex(std::size_t pathIndex) const;

    /// @brief Posts the main-thread task that points the records of merged members at their group's path.
    void retargetMergedMembers(unsigned generation);

    /**
     * @brief Runs the patchers over a staged copy of the mesh for one member, see PGPatcher::processNIF().
     *
     * @param shouldCompareWithOriginal Whether a result equal to the original is dropped instead of becoming an
     * output, see PGMeshPermutationTracker::commitMesh().
     */
    [[nodiscard]] bool patchMember(PGMeshPermutationTracker& tracker,
                                   std::size_t member,
                                   std::size_t pathIndex,
                                   MemberOutput& output,
                                   bool shouldCompareWithOriginal = true);

    std::mutex m_patchMutex;
    std::atomic<State> m_state { State::Unpatched };
    /// @brief The attempts of the first patch that could not read an input, and when the last one failed, see bytes().
    /// Under m_patchMutex.
    unsigned m_readFailures = 0;
    std::chrono::steady_clock::time_point m_readFailureTime;
    /// @brief The outcome per file and member, once patched.
    std::vector<std::vector<MemberOutput>> m_outputs;
    /// @brief Per member, the first member whose results it shares in every file (itself when it leads).
    std::vector<std::size_t> m_leaders;
};

/**
 * @brief The permutation plan of a session (DESIGN.md section 4.4): the families of every used mesh, their contexts
 * and the paths registered for them.
 */
class Plan {
public:
    /// @brief What a registered path serves.
    struct Target {
        Family* family = nullptr;
        std::size_t member = 0;
        std::size_t pathIndex = 0;
    };

    explicit Plan(unsigned generation);

    /**
     * @brief Plans every used mesh from the records alone: families, contexts and variant numbers. At data load, or
     * under the exclusive pipeline lock of Apply.
     *
     * @param uses The record model uses.
     * @param params The allowed record types, the mesh block list and allow list.
     * @param variantCounter The session counter the variant numbers come from; advanced by every variant.
     */
    void build(const Uses& uses,
               const PGParams& params,
               unsigned& variantCounter);

    /**
     * @brief Points the records of every member with a variant path at it, through the snapshot, which keeps a value
     * another plugin wrote. Only the families whose streams are registered: after build() and the registration, at
     * data load or under the exclusive pipeline lock of Apply.
     */
    void retargetRecords(RecordSnapshot& snapshot) const;

    /**
     * @brief Adds the family of a facegen head: one member for the dummy use the offline program patches it for.
     *
     * @return The family, whose single path is the given one.
     */
    Family& addFacegenFamily(const std::filesystem::path& path);

    /// @brief The families, in planning order.
    [[nodiscard]] const std::vector<std::unique_ptr<Family>>& families() const { return m_families; }

    /// @brief What a registered path serves, or nullptr for a path of no family.
    [[nodiscard]] Target* target(const std::string& path);

    /// @brief The plan generation, which deferred record edits of an older plan check.
    [[nodiscard]] unsigned generation() const { return m_generation; }

    /// @brief The number of variant paths of the plan.
    [[nodiscard]] std::size_t variantCount() const { return m_variantCount; }

    /// @brief The number of families whose patch ran and succeeded.
    [[nodiscard]] std::size_t patchedFamilyCount() const;

private:
    void addTargets(Family& family);

    unsigned m_generation;
    std::vector<std::unique_ptr<Family>> m_families;
    std::unordered_map<std::string, Target> m_targets;
    mutable std::mutex m_targetsMutex;
    std::size_t m_variantCount = 0;
};

}
