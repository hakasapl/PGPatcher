#pragma once

#include "NifFile.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/TaskQueue.hpp"
#include "util/TaskTracker.hpp"

#include <DirectXTex.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
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
#include <winnt.h>

class PGModManager;

/**
 * @brief Extends BethesdaDirectory to manage NIF mesh and DDS texture file mapping for a Bethesda game load order.
 *
 * Discovers and categorizes all relevant files (meshes, textures, PBR JSONs, Light Placer JSONs) from the
 * load order, deduces texture types from shader assignments, and maintains per-texture attribute and type metadata.
 */
class PGDirectory : public BethesdaDirectory {
public:
    struct NifCache {
        std::vector<std::pair<int, PGTypes::TextureSet>> textureSets;
        std::shared_ptr<nifly::NifFile> nif; // keep nif in cache to avoid reloading it multiple times
        unsigned long long origCRC32 = 0; // CRC32 of the NIF file
        std::vector<std::pair<PGMeshPermutationTracker::FormKey, PGPlugin::MeshUseAttributes>>
            meshUses; // list of mesh uses
    };

private:
    struct UnconfirmedTextureProperty {
        std::unordered_map<PGEnums::TextureSlots, size_t> slots;
        std::unordered_map<PGEnums::TextureType, size_t> types;
    };

    // Temp Structures
    std::unordered_map<std::filesystem::path, UnconfirmedTextureProperty> m_unconfirmedTextures;
    std::mutex m_unconfirmedTexturesMutex;
    std::unordered_set<std::filesystem::path> m_unconfirmedMeshes;

    struct TextureDetails {
        PGEnums::TextureType type;
        std::unordered_set<PGEnums::TextureAttribute> attributes;
    };

    // Structures to store relevant files (sometimes their contents)
    std::array<std::map<std::wstring, std::unordered_set<PGTypes::PGTexture, PGTypes::PGTextureHasher>>,
               NUM_TEXTURE_SLOTS>
        m_textureMaps;
    std::unordered_map<std::filesystem::path, TextureDetails> m_textureTypes;
    std::unordered_map<std::filesystem::path, NifCache> m_meshes;
    std::unordered_set<std::filesystem::path> m_textures;
    std::vector<std::filesystem::path> m_pbrJSONs;
    std::vector<std::filesystem::path> m_lightPlacerJSONs;

    // Mutexes
    std::shared_mutex m_textureMapsMutex;
    std::shared_mutex m_textureTypesMutex;
    std::shared_mutex m_meshesMutex;
    std::shared_mutex m_texturesMutex;

    TaskQueue m_meshUseMappingQueue;
    TaskQueue m_CMClassificationQueue;

public:
    /**
     * @brief Constructs a PGDirectory using a BethesdaGame to resolve data paths.
     *
     * @param bg Pointer to the BethesdaGame instance providing game/data paths.
     * @param outputPath Optional output directory path; defaults to empty (use game data path).
     */
    PGDirectory(BethesdaGame* bg,
                std::filesystem::path outputPath = "");

    /**
     * @brief Constructs a PGDirectory using an explicit data directory path.
     *
     * @param dataPath Absolute path to the Bethesda data directory.
     * @param outputPath Optional output directory path; defaults to empty (use dataPath).
     */
    PGDirectory(std::filesystem::path dataPath,
                std::filesystem::path outputPath = "");

    /// @brief Map all files in the load order to their type
    ///
    /// @param nifBlocklist Nifs to ignore for populating the mesh list
    /// @param manualTextureMaps Overwrite the type of a texture
    /// @param parallaxBSAExcludes Parallax maps contained in any of these BSAs are not considered for the file map
    /// @param mapFromMeshes The texture type is deducted from the shader/texture set it is assigned to, if false use
    /// only file suffix to determine type
    /// @param multithreading Speedup mapping by multithreading
    /// @param cacheNIFs Faster but higher memory consumption
    auto mapFiles(const std::vector<std::wstring>& nifBlocklist,
                  const std::vector<std::wstring>& nifAllowlist,
                  const std::vector<std::pair<std::wstring,
                                              PGEnums::TextureType>>& manualTextureMaps,
                  const std::vector<std::wstring>& parallaxBSAExcludes,
                  const bool& multithreading = true,
                  const bool& highmem = false,
                  const std::function<void(size_t,
                                           size_t)>& progressCallback
                  = {}) -> void;

    /**
     * @brief Blocks until all background plugin mesh-use mapping tasks have completed, then shuts down the queue.
     */
    void waitForMeshMapping();

    /**
     * @brief Blocks until all background Complex Material classification tasks have completed, then shuts down the
     * queue.
     */
    void waitForCMClassification();

private:
    auto findFiles() -> void;

    auto mapTexturesFromNIF(const std::filesystem::path& nifPath,
                            const bool& cachenif = false,
                            const bool& multithreading = true) -> TaskTracker::Result;

    auto updateUnconfirmedTexturesMap(const std::filesystem::path& path,
                                      const PGEnums::TextureSlots& slot,
                                      const PGEnums::TextureType& type) -> void;

    auto addToTextureMaps(const std::filesystem::path& path,
                          const PGEnums::TextureSlots& slot,
                          const PGEnums::TextureType& type,
                          const std::unordered_set<PGEnums::TextureAttribute>& attributes) -> void;

    void updateNifCache(const std::filesystem::path& path,
                        const std::vector<std::pair<int,
                                                    PGTypes::TextureSet>>& txstSets);
    void updateNifCache(const std::filesystem::path& path,
                        const std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                    PGPlugin::MeshUseAttributes>>& meshUses);
    void updateNifCache(const std::filesystem::path& path,
                        const std::shared_ptr<nifly::NifFile>& nif,
                        const unsigned long long& crc32);

    void checkIfCMAddToMap(const std::filesystem::path& texture,
                           const PGEnums::TextureSlots& winningSlot);

public:
    static auto checkGlobMatchInVector(const std::wstring& check,
                                       const std::vector<std::wstring>& list) -> bool;

    /// @brief Get the texture map for a given texture slot
    ///
    /// To populate the map call populateFileMap() and mapFiles().
    ///
    /// The key is the texture path without the suffix, the value is a set of texture paths.
    /// There can be more than one textures for a name without the suffix, since there are more than one possible
    /// suffixes for certain texture slots. Full texture paths are stored in each item of the value set.
    ///
    /// The decision between the two is handled later in the patching step. This ensures a _m doesn�t get replaced with
    /// an _em in the mesh for example (if both are cm) Because usually the existing thing in the slot is what is wanted
    /// if there are 2 or more possible options.
    ///
    /// Entry example:
    /// textures\\landscape\\dirtcliffs\\dirtcliffs01 -> {textures\\landscape\\dirtcliffs\\dirtcliffs01_mask.dds,
    /// textures\\landscape\\dirtcliffs\\dirtcliffs01.dds}
    ///
    /// @param Slot texture slot of BSShaderTextureSet in the shapes
    /// @return The mutable map
    [[nodiscard]] auto getTextureMap(const PGEnums::TextureSlots& slot)
        -> std::map<std::wstring,
                    std::unordered_set<PGTypes::PGTexture,
                                       PGTypes::PGTextureHasher>>&;

    /// @brief Get the immutable texture map for a given texture slot
    /// @see getTextureMap
    /// @param Slot texture slot of BSShaderTextureSet in the shapes
    /// @return The immutable map
    [[nodiscard]] auto getTextureMapConst(const PGEnums::TextureSlots& slot) const
        -> const std::map<std::wstring,
                          std::unordered_set<PGTypes::PGTexture,
                                             PGTypes::PGTextureHasher>>&;

    /**
     * @brief Returns the map of all discovered NIF mesh files and their cached data.
     *
     * @return Const reference to the mesh map keyed by relative path.
     */
    [[nodiscard]] auto getMeshes() const -> const std::unordered_map<std::filesystem::path,
                                                                     NifCache>&;

    /**
     * @brief Returns the set of all discovered DDS texture file paths.
     *
     * @return Const reference to the set of texture paths.
     */
    [[nodiscard]] auto getTextures() const -> const std::unordered_set<std::filesystem::path>&;

    /**
     * @brief Returns the ordered list of PBR NIF Patcher JSON configuration file paths.
     *
     * @return Const reference to the vector of PBR JSON paths.
     */
    [[nodiscard]] auto getPBRJSONs() const -> const std::vector<std::filesystem::path>&;

    /**
     * @brief Returns the ordered list of Light Placer JSON configuration file paths.
     *
     * @return Const reference to the vector of Light Placer JSON paths.
     */
    [[nodiscard]] auto getLightPlacerJSONs() const -> const std::vector<std::filesystem::path>&;

    /**
     * @brief Adds a texture attribute flag to the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to add.
     * @return true if the attribute was newly inserted; false if it already existed or the path is unknown.
     */
    auto addTextureAttribute(const std::filesystem::path& path,
                             const PGEnums::TextureAttribute& attribute) -> bool;

    /**
     * @brief Removes a texture attribute flag from the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to remove.
     * @return true if the attribute was present and removed; false otherwise.
     */
    auto removeTextureAttribute(const std::filesystem::path& path,
                                const PGEnums::TextureAttribute& attribute) -> bool;

    /**
     * @brief Checks whether a given attribute flag is set for the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to check.
     * @return true if the attribute is present; false otherwise.
     */
    [[nodiscard]] auto hasTextureAttribute(const std::filesystem::path& path,
                                           const PGEnums::TextureAttribute& attribute) -> bool;

    /**
     * @brief Returns all attribute flags currently set for the specified texture path.
     *
     * @param path Relative texture path.
     * @return Set of TextureAttribute values, or an empty set if the path is unknown.
     */
    [[nodiscard]] auto getTextureAttributes(const std::filesystem::path& path)
        -> std::unordered_set<PGEnums::TextureAttribute>;

    /**
     * @brief Sets the texture type for the specified texture path.
     *
     * @param path Relative texture path.
     * @param type The TextureType to assign.
     */
    void setTextureType(const std::filesystem::path& path,
                        const PGEnums::TextureType& type);

    /**
     * @brief Returns the texture type assigned to the specified texture path.
     *
     * @param path Relative texture path.
     * @return The assigned TextureType, or TextureType::UNKNOWN if the path is not tracked.
     */
    auto getTextureType(const std::filesystem::path& path) -> PGEnums::TextureType;
};
