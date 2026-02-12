#pragma once

#include "ParallaxGenPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <shared_mutex>
#include <shlwapi.h>
#include <unordered_map>
#include <vector>
#include <winnt.h>
/**
 * @class PatcherMeshShaderComplexMaterial
 * @brief Shader patcher for complex material
 */
class PatcherMeshShaderComplexMaterial : public PatcherMeshShader {
private:
    static std::shared_mutex s_metaCacheMutex; /** Mutex for material meta cache */
    static std::unordered_map<std::filesystem::path, nlohmann::json> s_metaCache; /** Cache for material meta */

public:
    static inline const std::filesystem::path s_DYNCUBEMAPPATH = "textures\\cubemaps\\dynamic1pxcubemap_black.dds";

    /**
     * @brief Get the Factory object
     *
     * @return PatcherShader::PatcherShaderFactory factory object for this patcher
     */
    static auto getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory;

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
    auto canApply(nifly::NiShape& nifShape, bool singlepassMATO,
        const ParallaxGenPlugin::ModelRecordType& modelRecordType) -> bool override;

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
    void applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape, const PatcherMatch& match) override;

    /**
     * @brief Apply the CM shader to the slots
     *
     * @param oldSlots Slots to apply to
     * @param match Match to apply
     * @return NIFUtil::TextureSet New slots after patching
     */
    void applyPatchSlots(NIFUtil::TextureSet& slots, const PatcherMatch& match) override;

    /**
     * @brief Apply CM shader to a shape
     *
     * @param nifShape Shape to apply shader to
     * @param nifModified Whether the NIF was modified
     */
    void applyShader(nifly::NiShape& nifShape) override;

private:
    static auto getMaterialMeta(const std::filesystem::path& envMaskPath) -> nlohmann::json;
};
