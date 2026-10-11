#pragma once

#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <shared_mutex>
#include <shlwapi.h>
#include <string>
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

    // Options.
    inline static bool s_disableDynCubemap = false;

public:
    static inline const std::filesystem::path s_dynCubemapPath = "textures\\cubemaps\\dynamic1pxcubemap_black.dds";

    /**
     * @brief Get the Factory object
     *
     * @return PatcherShader::PatcherShaderFactory factory object for this patcher
     */
    static PatcherMeshShader::PatcherMeshShaderFactory factory();

    /**
     * @brief Get the shader type for this patcher (CM)
     *
     * @return PGEnums::ShapeShader CM shader type
     */
    static PGEnums::ShapeShader shaderType();

    /**
     * @brief Load options for this patcher from pgtools option strings
     *
     * @param optionsStr Options by name; "disable_dyncubemap" disables the dynamic cubemap
     */
    static void loadOptions(std::unordered_map<std::string,
                                               std::string>& optionsStr);

    /**
     * @brief Load any options for all instances of this patcher
     *
     * @param disableDynCubemap Whether to disable dynamic cubemap
     */
    static void loadOptions(bool disableDynCubemap);

    /**
     * @brief Construct a new complex material patcher object
     *
     * @param nifPath Path to NIF file
     * @param nif NIF object to patch
     */
    PatcherMeshShaderComplexMaterial(std::filesystem::path nifPath,
                                     nifly::NifFile* nif);

    /**
     * @brief Check if the shape can accomodate the CM shader (without looking at texture slots)
     *
     * Grass, shader types other than default, environment map and parallax, and lighting flag combinations the CM
     * shader does not support are rejected.
     *
     * @param nifShape Shape to check
     * @return true Shape can accomodate CM
     * @return false Shape cannot accomodate CM
     */
    bool canApply(nifly::NiShape& nifShape,
                  bool isSinglepassMATO,
                  const PGPlugin::ModelRecordType& modelRecordType) override;

    /**
     * @brief Check if shape can accomodate CM shader based on texture slots only
     *
     * @param nifShape Shape to check
     * @param matches Vector of matches
     * @return true Match found
     * @return false No match found
     */
    bool shouldApply(nifly::NiShape& nifShape,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Find the complex material textures that match the normal map (or, failing that, the diffuse map) and have
     * its aspect ratio. A match equal to the current env mask slot is listed last
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches, each carrying the texture's material meta JSON as extra data when it has one
     * @return true Match found
     * @return false No match found
     */
    bool shouldApply(const PGTypes::TextureSet& oldSlots,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Apply the CM shader to the shape: shader type and flags, specular settings from the texture's attributes
     * and material meta, then the slots
     *
     * @param[in,out] slots Slots of the shape, updated with the match
     * @param nifShape Shape to apply to
     * @param[in] match Match to apply
     */
    void applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape,
                    const PatcherMatch& match) override;

    /**
     * @brief Apply the CM match to the slots: the env mask slot takes the match, the parallax slot is cleared and the
     * cubemap slot gets the dynamic cubemap unless disabled by option or material meta
     *
     * @param[in,out] slots Slots to apply to
     * @param match Match to apply
     */
    void applyPatchSlots(PGTypes::TextureSet& slots,
                         const PatcherMatch& match) override;

    /**
     * @brief Set the environment map shader type and flags on a shape
     *
     * @param nifShape Shape to apply shader to
     */
    void applyShader(nifly::NiShape& nifShape) override;

    /**
     * @brief Hash of the material meta JSON attached to a match
     */
    [[nodiscard]] uint64_t matchExtraDataHash(const PatcherMatch& match) const override;

private:
    /**
     * @brief Read the material meta JSON next to a complex material texture (same path with .json extension). Cached
     *
     * @return the JSON, null when there is none or it cannot be parsed
     */
    static nlohmann::json materialMeta(const std::filesystem::path& envMaskPath);
};
