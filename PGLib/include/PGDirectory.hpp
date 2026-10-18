#pragma once

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
    /// @brief What is known about a mesh of the load order.
    struct NifCache {
        std::vector<std::pair<PGMeshPermutationTracker::FormKey, PGPlugin::MeshUseAttributes>>
            meshUses; // list of mesh uses
    };

private:
    struct UnconfirmedTextureProperty {
        std::unordered_map<PGEnums::TextureSlots, size_t> slots;
        std::unordered_map<PGEnums::TextureType, size_t> types;
    };

    static constexpr size_t numUnconfirmedTextureMutexes = 64;

    // Temp Structures.
    std::unordered_map<std::filesystem::path, UnconfirmedTextureProperty> m_unconfirmedTextures;
    std::array<std::mutex, numUnconfirmedTextureMutexes>
        m_unconfirmedTextureMutexes; /**< Every unconfirmed texture is guarded by one of these, chosen by texture */
    std::unordered_set<std::filesystem::path> m_unconfirmedMeshes;

    struct TextureDetails {
        PGEnums::TextureType type = PGEnums::TextureType::Unknown;
        std::unordered_set<PGEnums::TextureAttribute> attributes;
    };

    // Structures to store relevant files (sometimes their contents).
    std::array<std::map<std::wstring, std::unordered_set<PGTypes::PGTexture, PGTypes::PGTextureHasher>>,
               numTextureSlots>
        m_textureMaps;
    std::unordered_map<std::filesystem::path, TextureDetails> m_textureTypes;
    std::unordered_map<std::filesystem::path, NifCache> m_meshes;
    std::unordered_set<std::filesystem::path> m_textures;
    std::vector<std::filesystem::path> m_pbrJSONs;
    std::vector<std::filesystem::path> m_lightPlacerJSONs;

    // Mutexes.
    std::shared_mutex m_textureMapsMutex;
    std::shared_mutex m_textureTypesMutex;
    std::shared_mutex m_meshesMutex;
    std::shared_mutex m_texturesMutex;

    TaskQueue m_meshUseMappingQueue;
    TaskQueue m_cmClassificationQueue;

public:
    /**
     * @brief Constructs a PGDirectory using a BethesdaGame to resolve data paths. PGGlobals::s_foldersToMap are mapped.
     *
     * @param bg Pointer to the BethesdaGame instance providing game/data paths. Must outlive this object.
     * @param outputPath Output directory that generated files are read from and written to.
     */
    explicit PGDirectory(BethesdaGame* bg,
                         std::filesystem::path outputPath = "");

    /**
     * @brief Constructs a PGDirectory for a plain data folder without a game (loose files only).
     *
     * @param dataPath Absolute path to the data directory.
     * @param outputPath Output directory that generated files are read from and written to.
     */
    explicit PGDirectory(std::filesystem::path dataPath,
                         std::filesystem::path outputPath = "");

    /// @brief Finds the meshes, textures, PBR JSONs and Light Placer JSONs of the file map and classifies every texture
    ///
    /// The slot and type of a texture are decided by a vote of the shapes that use it; a texture no mesh uses is
    /// classified by its file name suffix. A height map or an environment mask whose JSON (its path with the extension
    /// replaced by .json) holds "disabled": true is left out like a texture set to unknown by a manual texture map,
    /// unless a manual texture map of its own gives its type. Environment masks are then classified as complex material
    /// textures on the GPU, on a background queue when multithreading (see waitForCMClassification()). Plugin uses of
    /// the meshes are looked up on another background queue (see waitForMeshMapping()). Requires a populated file map.
    ///
    /// @param nifBlocklist Globs of meshes to skip
    /// @param nifAllowlist Globs of meshes to map; everything else is skipped when it is not empty
    /// @param manualTextureMaps Textures whose type is given instead of classified
    /// @param parallaxBSAExcludes Height maps and environment masks from these archives are not mapped (vanilla
    /// archives, whose textures must not be taken for parallax or complex material textures)
    /// @param multithreading Read the meshes on the task pool
    /// @param progressCallback Receives (completed, total) counts while the meshes are read
    /// @throws std::runtime_error if the file map was not populated
    void mapFiles(const std::vector<std::wstring>& nifBlocklist,
                  const std::vector<std::wstring>& nifAllowlist,
                  const std::vector<std::pair<std::wstring,
                                              PGEnums::TextureType>>& manualTextureMaps,
                  const std::vector<std::wstring>& parallaxBSAExcludes,
                  const bool& multithreading = true,
                  const std::function<void(size_t,
                                           size_t)>& progressCallback = { });

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
    /**
     * @brief Collects the meshes, textures (ASCII paths only), PBR JSONs and Light Placer JSONs from the file map
     *
     * @throws std::runtime_error if the file map was not populated
     */
    void findFiles();

    /**
     * @brief Decides the slot and type of every unconfirmed texture from the votes cast so far (the file name suffix
     * when there are none), applies the manual texture maps, the texture JSONs and the vanilla archive exclusion, and
     * adds the textures to the texture maps. Environment masks go through the complex material classification, on the
     * classification queue when multithreading.
     */
    void classifyUnconfirmedTextures(const std::unordered_map<std::wstring,
                                                              PGEnums::TextureType>& manualTextureMapsMap,
                                     const std::vector<std::wstring>& parallaxBSAExcludes,
                                     const bool& multithreading);

    /**
     * @brief Checks whether the JSON of a height map (parallax JSON) or of an environment mask (complex material JSON)
     * disables the texture: the texture's path with the extension replaced by .json holds "disabled": true. A JSON that
     * cannot be read or parsed is logged and disables nothing.
     *
     * @param texture Texture to check
     * @param type Type the texture was classified as; textures of other types have no such JSON
     * @return true if the texture must be left out of the texture maps
     */
    bool isDisabledByJSON(const std::filesystem::path& texture,
                          const PGEnums::TextureType& type);

    /**
     * @brief Casts the texture votes of one mesh (from the update cache when the mesh is unchanged), queues the lookup
     * of its plugin uses and marks its mod as having meshes
     *
     * @return Result::Failure if the mesh could not be read
     */
    TaskTracker::Result mapTexturesFromNIF(const std::filesystem::path& nifPath,
                                           const bool& multithreading = true);

    /**
     * @brief Reads a NIF and collects the (texture, slot, type) votes of every shader patchable shape
     *
     * @param nifPath Relative path of the NIF
     * @param[out] votes Collected votes
     * @return true if the NIF could be read
     * @return false if the NIF could not be read
     */
    bool readTextureVotesFromNIF(const std::filesystem::path& nifPath,
                                 std::vector<PGTypes::TextureVote>& votes);

    /**
     * @brief Adds an environment mask texture to the texture maps according to its complex material classification
     */
    void applyCMClassification(const std::filesystem::path& texture,
                               const PGEnums::TextureSlots& winningSlot,
                               const PGTypes::CMClassification& classification);

    /**
     * @brief Counts one vote for the slot and type of a texture. Thread safe; votes for unknown textures are dropped
     */
    void updateUnconfirmedTexturesMap(const std::filesystem::path& path,
                                      const PGEnums::TextureSlots& slot,
                                      const PGEnums::TextureType& type);

    /**
     * @brief Records the final slot, type and attributes of a texture and tells the mod manager which shader the
     * texture's mod provides
     */
    void addToTextureMaps(const std::filesystem::path& path,
                          const PGEnums::TextureSlots& slot,
                          const PGEnums::TextureType& type,
                          const std::unordered_set<PGEnums::TextureAttribute>& attributes);

    /**
     * @brief Stores the plugin uses of a mesh. Thread safe
     */
    void updateNifCache(const std::filesystem::path& path,
                        const std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                    PGPlugin::MeshUseAttributes>>& meshUses);

    /**
     * @brief Classifies an environment mask on the GPU, stores the result in the update cache and adds the texture to
     * the texture maps. A texture that cannot be processed is logged and left out
     */
    void checkIfCMAddToMap(const std::filesystem::path& texture,
                           const PGEnums::TextureSlots& winningSlot);

public:
    /**
     * @brief Checks whether a string matches any glob of a list (MS-DOS wildcards, see PathMatchSpecW)
     */
    static bool checkGlobMatchInVector(const std::wstring& check,
                                       const std::vector<std::wstring>& list);

    /// @brief Get the texture map for a given texture slot
    ///
    /// To populate the map call populateFileMap() and mapFiles().
    ///
    /// The key is the texture path without the suffix, the value is a set of texture paths.
    /// There can be more than one textures for a name without the suffix, since there are more than one possible
    /// suffixes for certain texture slots. Full texture paths are stored in each item of the value set.
    ///
    /// The decision between the two is handled later in the patching step. This ensures a _m doesn't get replaced with
    /// an _em in the mesh for example (if both are cm) Because usually the existing thing in the slot is what is wanted
    /// if there are 2 or more possible options.
    ///
    /// Entry example:
    /// textures\\landscape\\dirtcliffs\\dirtcliffs01 -> {textures\\landscape\\dirtcliffs\\dirtcliffs01_mask.dds,
    /// textures\\landscape\\dirtcliffs\\dirtcliffs01.dds}
    ///
    /// @param Slot texture slot of BSShaderTextureSet in the shapes
    /// @return The mutable map
    [[nodiscard]] std::map<std::wstring,
                           std::unordered_set<PGTypes::PGTexture,
                                              PGTypes::PGTextureHasher>>&
    textureMap(const PGEnums::TextureSlots& slot);

    /// @brief Get the immutable texture map for a given texture slot
    /// @see textureMap
    /// @param Slot texture slot of BSShaderTextureSet in the shapes
    /// @return The immutable map
    [[nodiscard]] const std::map<std::wstring,
                                 std::unordered_set<PGTypes::PGTexture,
                                                    PGTypes::PGTextureHasher>>&
    textureMapConst(const PGEnums::TextureSlots& slot) const;

    /**
     * @brief Returns the meshes that were mapped, with their plugin uses. Complete once waitForMeshMapping() returned.
     *
     * @return Const reference to the mesh map keyed by lower case relative path.
     */
    [[nodiscard]] const std::unordered_map<std::filesystem::path,
                                           NifCache>&
    meshes() const;

    /**
     * @brief Returns the set of all discovered DDS texture file paths.
     *
     * @return Const reference to the set of texture paths.
     */
    [[nodiscard]] const std::unordered_set<std::filesystem::path>& textures() const;

    /**
     * @brief Returns the ordered list of PBR NIF Patcher JSON configuration file paths.
     *
     * @return Const reference to the vector of PBR JSON paths.
     */
    [[nodiscard]] const std::vector<std::filesystem::path>& pbrJSONs() const;

    /**
     * @brief Gives a mod manager what the mapping gave the installed one (PGGlobals::pgmm()): the shader of every
     * mapped texture's mod as addToTextureMaps() assigned it, TruePBR for the mods of the TruePBR configs, and
     * hasMeshes for the mods of the meshes the mesh reading pass read. For a mod list built from other mod manager
     * settings than the installed ones (the PGLite menu). Call it on the installed directory (PGGlobals::pgd()), which
     * the mod manager resolves archived files through, and while nothing replaces it; the patches that run meanwhile
     * are fine, the maps it reads are locked.
     *
     * @param modManager The mod manager to attribute, with its mod file map populated.
     */
    void attributeMods(PGModManager& modManager);

    /**
     * @brief Returns the ordered list of Light Placer JSON configuration file paths.
     *
     * @return Const reference to the vector of Light Placer JSON paths.
     */
    [[nodiscard]] const std::vector<std::filesystem::path>& lightPlacerJSONs() const;

    /**
     * @brief Adds a texture attribute flag to the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to add.
     * @return true if the attribute was newly inserted; false if it already existed or the path is unknown.
     */
    bool addTextureAttribute(const std::filesystem::path& path,
                             const PGEnums::TextureAttribute& attribute);

    /**
     * @brief Removes a texture attribute flag from the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to remove.
     * @return true if the attribute was present and removed; false otherwise.
     */
    bool removeTextureAttribute(const std::filesystem::path& path,
                                const PGEnums::TextureAttribute& attribute);

    /**
     * @brief Checks whether a given attribute flag is set for the specified texture path.
     *
     * @param path Relative texture path.
     * @param attribute Attribute to check.
     * @return true if the attribute is present; false otherwise.
     */
    [[nodiscard]] bool hasTextureAttribute(const std::filesystem::path& path,
                                           const PGEnums::TextureAttribute& attribute);

    /**
     * @brief Returns all attribute flags currently set for the specified texture path.
     *
     * @param path Relative texture path.
     * @return Set of TextureAttribute values, or an empty set if the path is unknown.
     */
    [[nodiscard]] std::unordered_set<PGEnums::TextureAttribute> textureAttributes(const std::filesystem::path& path);

    /**
     * @brief Sets the texture type for the specified texture path.
     *
     * @param path Relative texture path.
     * @param type The TextureType to assign.
     */
    void setTextureType(const std::filesystem::path& path,
                        const PGEnums::TextureType& type);

    /**
     * @brief Returns the texture type assigned to the specified texture path. The lookup is recorded for the update
     * cache, like hasTextureAttribute() and textureAttributes().
     *
     * @param path Relative texture path.
     * @return The assigned TextureType, or TextureType::Unknown if the path is not tracked.
     */
    PGEnums::TextureType textureType(const std::filesystem::path& path);
};
