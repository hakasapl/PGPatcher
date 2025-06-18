#pragma once

#include <Geometry.hpp>
#include <NifFile.hpp>

#include <vector>

#include "NIFUtil.hpp"
#include "patchers/base/PatcherMesh.hpp"

/**
 * @class PatcherMeshShader
 * @brief Base class for shader patchers
 */
class PatcherMeshShader : public PatcherMesh {
private:
    struct PatchedTextureSetsHash {
        auto operator()(const std::tuple<std::filesystem::path, uint32_t>& key) const -> std::size_t
        {
            return std::hash<std::filesystem::path>()(std::get<0>(key)) ^ std::hash<uint32_t>()(std::get<1>(key));
        }
    };

    struct PatchedTextureSet {
        NIFUtil::TextureSet original;
        std::unordered_map<uint32_t, NIFUtil::TextureSet> patchResults;
    };

    static std::shared_mutex s_patchedTextureSetsMutex;
    static std::unordered_map<std::tuple<std::filesystem::path, uint32_t>, PatchedTextureSet, PatchedTextureSetsHash>
        s_patchedTextureSets;

public:
    static auto getTextureSet(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape)
        -> NIFUtil::TextureSet;
    static auto setTextureSet(const std::filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape,
        const NIFUtil::TextureSet& textures) -> bool;

    // type definitions
    using PatcherMeshShaderFactory
        = std::function<std::unique_ptr<PatcherMeshShader>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshShaderObject = std::unique_ptr<PatcherMeshShader>;

    /**
     * @struct PatcherMatch
     * @brief Structure to store the matched texture and the texture slots it matched with
     */
    struct PatcherMatch {
        std::wstring matchedPath; // The path of the matched file
        std::unordered_set<NIFUtil::TextureSlots> matchedFrom; // The texture slots that the match matched with
        std::shared_ptr<void> extraData; // Any extra data the patcher might need intermally to do the patch
    };

    // Constructors
    PatcherMeshShader(std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName);
    virtual ~PatcherMeshShader() = default;
    PatcherMeshShader(const PatcherMeshShader& other) = default;
    auto operator=(const PatcherMeshShader& other) -> PatcherMeshShader& = default;
    PatcherMeshShader(PatcherMeshShader&& other) noexcept = default;
    auto operator=(PatcherMeshShader&& other) noexcept -> PatcherMeshShader& = default;

    /**
     * @brief Checks if a shape can be patched by this patcher (without looking at slots)
     *
     * @param nifShape Shape to check
     * @return true Shape can be patched
     * @return false Shape cannot be patched
     */
    virtual auto canApply(nifly::NiShape& nifShape) -> bool = 0;

    /// @brief  Methods that determine whether the patcher should apply to a shape
    /// @param[in] nifShape shape to check
    /// @param matches found matches
    /// @return if any match was found
    virtual auto shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool = 0;

    /// @brief determine if the patcher should be applied to the shape
    /// @param[in] oldSlots array of texture slot textures
    /// @param[out] matches vector of matches for the given textures
    /// @return if any match was found
    virtual auto shouldApply(const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches) -> bool = 0;

    // Methods that apply the patch to a shape
    virtual auto applyPatch(nifly::NiShape& nifShape, const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
        = 0;

    /// @brief apply the matched texture to the texture slots
    /// @param[in] oldSlots array of the slot textures
    /// @param[in] match matching texture
    /// @return new array containing the applied matched texture
    virtual auto applyPatchSlots(
        const NIFUtil::TextureSet& oldSlots, const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
        = 0;

    virtual void processNewTXSTRecord(const PatcherMatch& match, const std::string& edid = {}) = 0;

    /// @brief apply the shader to the shape
    /// @param[in] nifShape shape to apply the shader to
    virtual auto applyShader(nifly::NiShape& nifShape) -> bool = 0;
};
