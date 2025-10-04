#pragma once

#include <NifFile.hpp>
#include <filesystem>
#include <shlwapi.h>
#include <string>
#include <winnt.h>

#include "patchers/base/PatcherMeshShader.hpp"

/**
 * @class PatcherMeshShaderComplexMaterial
 * @brief Shader patcher for complex material
 */
class PatcherMeshShaderComplexMaterial : public PatcherMeshShader {
private:
    static std::vector<std::wstring> s_dynCubemapBlocklist; /** Stores the dynamic cubemap blocklist set */
    static bool s_disableMLP; /** If true MLP should be replaced with CM */

    static std::shared_mutex s_metaCacheMutex; /** Mutex for material meta cache */
    static std::unordered_map<std::filesystem::path, nlohmann::json> s_metaCache; /** Cache for material meta */

public:
    static inline const std::filesystem::path s_DYNCUBEMAPPATH = "textures/cubemaps/dynamic1pxcubemap_black.dds";

    /**
     * @brief Get the Factory object
     *
     * @return PatcherShader::PatcherShaderFactory factory object for this patcher
     */
    static auto getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory;

    /**
     * @brief Load required statics for CM patcher
     *
     * @param disableMLP If true MLP should be replaced with CM
     * @param dynCubemapBlocklist Set of blocklisted dynamic cubemaps
     */
    static void loadStatics(const bool& disableMLP, const std::vector<std::wstring>& dynCubemapBlocklist);

    /**
     * @brief Get the shader type for this patcher (CM)
     *
     * @return NIFUtil::ShapeShader CM shader type
     */
    static auto getShaderType() -> NIFUtil::ShapeShader;

    /**
     * @brief Construct a new complex material patcher object
     *
     * @param nifPath Path to NIF file
     * @param nif NIF object to patch
     */
    PatcherMeshShaderComplexMaterial(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Check if the shape can accomodate the CM shader (without looking at texture slots)
     *
     * @param nifShape Shape to check
     * @return true Shape can accomodate CM
     * @return false Shape cannot accomodate CM
     */
    auto canApply(nifly::NiShape& nifShape, bool singlepassMATO) -> bool override;

    /**
     * @brief Check if shape can accomodate CM shader based on texture slots only
     *
     * @param nifShape Shape to check
     * @param matches Vector of matches
     * @return true Match found
     * @return false No match found
     */
    auto shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool override;

    /**
     * @brief Check if slots can accomodate CM shader
     *
     * @param oldSlots Slots to check
     * @param matches Vector of matches
     * @return true Match found
     * @return false No match found
     */
    auto shouldApply(const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches) -> bool override;

    /**
     * @brief Apply the CM shader to the shape
     *
     * @param[in] nifShape Shape to apply to
     * @param[in] match Match to apply
     * @return NIFUtil::TextureSet New slots after patching
     */
    auto applyPatch(const NIFUtil::TextureSet& oldSlots, nifly::NiShape& nifShape, const PatcherMatch& match,
        NIFUtil::TextureSet& newSlots) -> bool override;

    /**
     * @brief Apply the CM shader to the slots
     *
     * @param oldSlots Slots to apply to
     * @param match Match to apply
     * @return NIFUtil::TextureSet New slots after patching
     */
    auto applyPatchSlots(const NIFUtil::TextureSet& oldSlots, const PatcherMatch& match, NIFUtil::TextureSet& newSlots)
        -> bool override;

    /**
     * @brief Apply CM shader to a shape
     *
     * @param nifShape Shape to apply shader to
     * @param nifModified Whether the NIF was modified
     */
    auto applyShader(nifly::NiShape& nifShape) -> bool override;

private:
    static auto getMaterialMeta(const std::filesystem::path& envMaskPath) -> nlohmann::json;
};
