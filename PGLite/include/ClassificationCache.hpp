#pragma once

#include "PGRunObserver.hpp"
#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGTypes.hpp"

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace PGLite {

/**
 * @brief The texture classification cache (DESIGN.md section 4.5): what the mesh reading pass and the complex material
 * classification found in a previous session, so a texture or a mesh that did not change is not read again.
 *
 * The mesh reading pass reads every mesh of the load order for the texture votes of its shapes, and the complex
 * material classification decodes every environment mask on the GPU; a load order has tens of thousands of both. Each
 * result is kept by file identity (the same identity the offline update cache uses: the modification time and size of
 * a loose file, the name, modification time and size of the archive of an archived file), so a session reads only new
 * or changed files. The file is written for the PGLite version that made it: another version starts empty, since the
 * votes depend on the code that casts them. The cache is the process's PGRunObserver; the lookups a patch makes are
 * not recorded, since nothing replays them.
 */
class ClassificationCache final : public PGRunObserver {
public:
    /// @brief Reads the cache file, if there is one. A file that cannot be read, or of another version, starts empty.
    void load(const std::filesystem::path& file);

    /**
     * @brief Writes the cache file when an entry was added since it was read. Only the entries this session looked up
     * or added are written, so files that left the load order leave the cache. Call it after the lookups are done.
     */
    void save() const;

    /// @brief The number of cached complex material classifications.
    [[nodiscard]] std::size_t cmCount() const;

    /// @brief The number of cached mesh votes.
    [[nodiscard]] std::size_t meshCount() const;

    bool tryGetCachedMeshVotes(const std::filesystem::path& nifPath,
                               const BethesdaDirectory::FileIdentity& identity,
                               std::vector<PGTypes::TextureVote>& votes) override;

    void storeMeshVotes(const std::filesystem::path& nifPath,
                        const BethesdaDirectory::FileIdentity& identity,
                        const std::vector<PGTypes::TextureVote>& votes) override;

    bool tryGetCachedCMClassification(const std::filesystem::path& texture,
                                      const BethesdaDirectory::FileIdentity& identity,
                                      PGTypes::CMClassification& result) override;

    void storeCMClassification(const std::filesystem::path& texture,
                               const BethesdaDirectory::FileIdentity& identity,
                               const PGTypes::CMClassification& result) override;

private:
    struct CMEntry {
        BethesdaDirectory::FileIdentity identity;
        PGTypes::CMClassification classification;
        /// @brief Looked up or added this session; the others are not written back.
        bool isCurrent = false;
    };

    struct MeshEntry {
        BethesdaDirectory::FileIdentity identity;
        std::vector<PGTypes::TextureVote> votes;
        bool isCurrent = false;
    };

    std::filesystem::path m_file;
    std::unordered_map<std::filesystem::path, CMEntry> m_cmEntries;
    std::unordered_map<std::filesystem::path, MeshEntry> m_meshEntries;
    mutable std::mutex m_mutex;
    mutable bool m_isDirty = false;
};

}
