#pragma once

#include "PGDirectory.hpp"
#include "PGRunObserver.hpp"
#include "PatchStream.hpp"
#include "pgutil/PGEnums.hpp"

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PGLite {

/**
 * @brief The textures the texture hook patchers generate (DESIGN.md section 4.12): the complex material map the
 * parallax to complex material transform makes from a height map, and the subsurface color map Fix SSS makes from a
 * diffuse.
 *
 * Offline both are written to the output folder in the texture phase, after every mesh. At runtime a texture is
 * generated the moment the first patched mesh that names it is produced, on the thread that patches, from the bytes
 * the engine itself serves for the source. The result is kept in a disk cache under the plugin's folder and served to
 * the engine through a registered stream, never through the data folder. The cache key holds everything the output
 * depends on: the source bytes, the shader and the parameters of the hook, the DirectXTex version and the PGLite
 * version. A generated texture is not a texture map entry, as offline: the maps hold the textures the patchers match,
 * and a generated texture is only reached through its hook.
 */
class TextureGenerator final : public PatchStream::Provider {
public:
    /// @brief The counts the status page shows.
    struct Stats {
        std::size_t generated = 0;
        std::size_t fromCache = 0;
        std::size_t failed = 0;
    };

    /**
     * @param cacheFolder The folder of the cache, created when the first texture is written.
     */
    explicit TextureGenerator(std::filesystem::path cacheFolder);

    /**
     * @brief Generates, or takes from the cache, every texture the hooks registered that this session has not made yet,
     * records its type with the directory and registers its stream. Called right after a family was patched, before
     * its bytes reach the engine, under the shared pipeline lock; one caller at a time, so a texture is made once.
     * Never throws: a texture that cannot be made is logged once and stays missing, as offline.
     */
    void generatePending();

    /**
     * @brief Gives a new environment's directory what the hooks and generatePending() gave the old one for every
     * texture of the session (the generated file and its type), and empties the hooks' lists so the next patch
     * registers its textures with the new directory again. Called under the exclusive pipeline lock.
     */
    void onEnvironmentReplaced(PGDirectory& directory);

    /// @brief The counts of this session. Any thread.
    [[nodiscard]] Stats stats() const;

    // PatchStream::Provider: the cache file of a generated texture.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> bytesFor(const std::string& path,
                                                                         SourceStream& source) override;

private:
    using HookKind = PGRunObserver::HookKind;

    /// @brief A texture of the session, made or failed.
    struct Entry {
        PGEnums::TextureType type = PGEnums::TextureType::Unknown;
        bool isServed = false;
    };

    /// @brief Makes one texture, see generatePending().
    void process(HookKind kind,
                 const std::filesystem::path& texPath);

    /// @brief The key of a cached texture: the lines that decide its content, see the class comment.
    [[nodiscard]] static std::string cacheKey(HookKind kind,
                                              const std::filesystem::path& texPath,
                                              const std::vector<std::byte>& source);

    /// @brief Whether the cache holds the texture of this key, with the size its key file recorded.
    [[nodiscard]] static bool isCached(const std::filesystem::path& cacheFile,
                                       const std::string& key);

    /**
     * @brief Generates the texture from the source bytes and writes it, then its key, each through a temporary file
     * and a rename, so a crash in between leaves a stale key and never a stale texture.
     *
     * @return false if the generation or the write of the texture failed (logged).
     */
    [[nodiscard]] static bool generate(HookKind kind,
                                       const std::filesystem::path& texPath,
                                       const std::vector<std::byte>& source,
                                       const std::filesystem::path& cacheFile,
                                       const std::string& key);

    std::filesystem::path m_cacheFolder;
    /// @brief The streams of generated textures have no engine stream to read; this one stands in for it.
    std::shared_ptr<SourceStream> m_noSource;

    /// @brief Serializes generatePending(); held while a texture is generated.
    std::mutex m_mutex;
    std::unordered_map<std::filesystem::path, Entry> m_entries;

    /// @brief The registered paths, as the engine names them, and their cache files, for bytesFor().
    mutable std::mutex m_cacheFilesMutex;
    std::unordered_map<std::string, std::filesystem::path> m_cacheFiles;

    std::atomic<std::size_t> m_generated { 0 };
    std::atomic<std::size_t> m_fromCache { 0 };
    std::atomic<std::size_t> m_failed { 0 };
};

}
