#pragma once

#include <DirectXTex.h>
#include <NifFile.hpp>
#include <array>
#include <filesystem>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <winnt.h>

#include "BethesdaDirectory.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenTask.hpp"
#include "util/NIFUtil.hpp"

class ModManagerDirectory;

class ParallaxGenDirectory : public BethesdaDirectory {
public:
    struct NifCache {
        std::vector<std::pair<int, NIFUtil::TextureSet>> textureSets;
    };

private:
    static constexpr int MAPTEXTURE_PROGRESS_MODULO = 10;

    struct UnconfirmedTextureProperty {
        std::unordered_map<NIFUtil::TextureSlots, size_t> slots;
        std::unordered_map<NIFUtil::TextureType, size_t> types;
    };

    // Temp Structures
    std::unordered_map<std::filesystem::path, UnconfirmedTextureProperty> m_unconfirmedTextures;
    std::mutex m_unconfirmedTexturesMutex;
    std::unordered_set<std::filesystem::path> m_unconfirmedMeshes;

    struct TextureDetails {
        NIFUtil::TextureType type;
        std::unordered_set<NIFUtil::TextureAttribute> attributes;
    };

    // Structures to store relevant files (sometimes their contents)
    std::array<std::map<std::wstring, std::unordered_set<NIFUtil::PGTexture, NIFUtil::PGTextureHasher>>,
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

public:
    // constructor - calls the BethesdaDirectory constructor
    ParallaxGenDirectory(BethesdaGame* bg, std::filesystem::path outputPath = "", ModManagerDirectory* mmd = nullptr);
    ParallaxGenDirectory(
        std::filesystem::path dataPath, std::filesystem::path outputPath = "", ModManagerDirectory* mmd = nullptr);

    /// @brief Map all files in the load order to their type
    ///
    /// @param nifBlocklist Nifs to ignore for populating the mesh list
    /// @param manualTextureMaps Overwrite the type of a texture
    /// @param parallaxBSAExcludes Parallax maps contained in any of these BSAs are not considered for the file map
    /// @param mapFromMeshes The texture type is deducted from the shader/texture set it is assigned to, if false use
    /// only file suffix to determine type
    /// @param multithreading Speedup mapping by multithreading
    /// @param cacheNIFs Faster but higher memory consumption
    auto mapFiles(const std::vector<std::wstring>& nifBlocklist, const std::vector<std::wstring>& nifAllowlist,
        const std::vector<std::pair<std::wstring, NIFUtil::TextureType>>& manualTextureMaps,
        const std::vector<std::wstring>& parallaxBSAExcludes, const bool& multithreading = true) -> void;

private:
    auto findFiles() -> void;

    auto mapTexturesFromNIF(const std::filesystem::path& nifPath) -> ParallaxGenTask::PGResult;

    auto updateUnconfirmedTexturesMap(
        const std::filesystem::path& path, const NIFUtil::TextureSlots& slot, const NIFUtil::TextureType& type) -> void;

    auto addToTextureMaps(const std::filesystem::path& path, const NIFUtil::TextureSlots& slot,
        const NIFUtil::TextureType& type, const std::unordered_set<NIFUtil::TextureAttribute>& attributes) -> void;

    auto addMesh(const std::filesystem::path& path, const NifCache& nifCache) -> void;

public:
    static auto checkGlobMatchInVector(const std::wstring& check, const std::vector<std::wstring>& list) -> bool;

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
    [[nodiscard]] auto getTextureMap(const NIFUtil::TextureSlots& slot)
        -> std::map<std::wstring, std::unordered_set<NIFUtil::PGTexture, NIFUtil::PGTextureHasher>>&;

    /// @brief Get the immutable texture map for a given texture slot
    /// @see getTextureMap
    /// @param Slot texture slot of BSShaderTextureSet in the shapes
    /// @return The immutable map
    [[nodiscard]] auto getTextureMapConst(const NIFUtil::TextureSlots& slot) const
        -> const std::map<std::wstring, std::unordered_set<NIFUtil::PGTexture, NIFUtil::PGTextureHasher>>&;

    [[nodiscard]] auto getMeshes() const -> const std::unordered_map<std::filesystem::path, NifCache>&;

    [[nodiscard]] auto getTextures() const -> const std::unordered_set<std::filesystem::path>&;

    [[nodiscard]] auto getPBRJSONs() const -> const std::vector<std::filesystem::path>&;

    [[nodiscard]] auto getLightPlacerJSONs() const -> const std::vector<std::filesystem::path>&;

    auto addTextureAttribute(const std::filesystem::path& path, const NIFUtil::TextureAttribute& attribute) -> bool;

    auto removeTextureAttribute(const std::filesystem::path& path, const NIFUtil::TextureAttribute& attribute) -> bool;

    [[nodiscard]] auto hasTextureAttribute(
        const std::filesystem::path& path, const NIFUtil::TextureAttribute& attribute) -> bool;

    [[nodiscard]] auto getTextureAttributes(const std::filesystem::path& path)
        -> std::unordered_set<NIFUtil::TextureAttribute>;

    void setTextureType(const std::filesystem::path& path, const NIFUtil::TextureType& type);

    auto getTextureType(const std::filesystem::path& path) -> NIFUtil::TextureType;
};
