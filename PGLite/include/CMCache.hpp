#pragma once

#include "PGRunObserver.hpp"
#include "common/BethesdaDirectory.hpp"
#include "pgutil/PGTypes.hpp"

#include <filesystem>
#include <mutex>
#include <unordered_map>

namespace PGLite {

/**
 * @brief The complex material classification cache (DESIGN.md section 4.5): the one thing PGLite persists.
 *
 * Classifying an environment mask decodes the texture on the GPU, and a load order has thousands of them, so the
 * results are kept on disk by file identity (the same identity the offline update cache uses) and a texture that did
 * not change is never decoded again. The cache is the process's PGRunObserver; the lookups a patch makes are not
 * recorded, since nothing replays them.
 */
class CMCache final : public PGRunObserver {
public:
    /// @brief Reads the cache file, if there is one. A file that cannot be read starts an empty cache.
    void load(const std::filesystem::path& file);

    /// @brief Writes the cache file when a classification was added since it was read.
    void save() const;

    /// @brief The number of cached classifications.
    [[nodiscard]] std::size_t size() const;

    bool tryGetCachedCMClassification(const std::filesystem::path& texture,
                                      const BethesdaDirectory::FileIdentity& identity,
                                      PGTypes::CMClassification& result) override;

    void storeCMClassification(const std::filesystem::path& texture,
                               const BethesdaDirectory::FileIdentity& identity,
                               const PGTypes::CMClassification& result) override;

private:
    struct Entry {
        BethesdaDirectory::FileIdentity identity;
        PGTypes::CMClassification classification;
    };

    std::filesystem::path m_file;
    std::unordered_map<std::filesystem::path, Entry> m_entries;
    mutable std::mutex m_mutex;
    mutable bool m_isDirty = false;
};

}
