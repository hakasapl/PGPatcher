#pragma once

#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>
#include <vector>

/**
 * @class PatcherMeshShaderVanillaParallax
 * @brief Patcher for vanilla parallax
 */
class PatcherMeshShaderVanillaParallax : public PatcherMeshShader {
private:
    bool m_hasAttachedHavok
        = false; /** Stores at the NIF level whether the NIF has attached havok (incompatible with parallax) */

public:
    /**
     * @brief Get the Factory object for parallax patcher
     *
     * @return PatcherShader::PatcherShaderFactory Factory object
     */
    static PatcherMeshShader::PatcherMeshShaderFactory factory();

    /**
     * @brief Get the Shader Type for this patcher (Parallax)
     *
     * @return PGEnums::ShapeShader Parallax
     */
    static PGEnums::ShapeShader shaderType();

    /**
     * @brief Construct a new Patcher Vanilla Parallax object
     *
     * @param nifPath NIF path to patch
     * @param nif NIF object to patch
     */
    PatcherMeshShaderVanillaParallax(std::filesystem::path nifPath,
                                     nifly::NifFile* nif);

    /**
     * @brief Check if a shape can take vanilla parallax (without looking at slots)
     *
     * Grass, single pass material objects, meshes with attached havok, skinned shapes, shapes with an alpha property,
     * decals, mesh lighting and anisotropic lighting, and shader types other than default, parallax and environment
     * map are rejected.
     *
     * @param nifShape Shape to check
     * @return true Shape can be patched
     * @return false Shape cannot be patched
     */
    bool canApply(nifly::NiShape& nifShape,
                  bool isSinglepassMATO,
                  const PGPlugin::ModelRecordType& modelRecordType) override;

    /**
     * @brief Find the matches for a shape's texture set, see the texture set overload
     *
     * @param nifShape Shape to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false No matches found
     */
    bool shouldApply(nifly::NiShape& nifShape,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Find the height maps that match the normal map (or, failing that, the diffuse map) and have its aspect
     * ratio. A match equal to the current parallax slot is listed last
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false No matches found
     */
    bool shouldApply(const PGTypes::TextureSet& oldSlots,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Apply a parallax match to a shape: the shader, then the slots
     *
     * @param[in,out] slots Slots of the shape, updated with the match
     * @param nifShape Shape to patch
     * @param match Match to apply
     */
    void applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape,
                    const PatcherMatch& match) override;

    /**
     * @brief Put the matched height map into the parallax slot
     *
     * @param[in,out] slots Slots to patch
     * @param match Match to apply
     */
    void applyPatchSlots(PGTypes::TextureSet& slots,
                         const PatcherMatch& match) override;

    /**
     * @brief Set the parallax shader type and flags on a shape and enable its vertex colors
     *
     * @param nifShape Shape to apply shader to
     */
    void applyShader(nifly::NiShape& nifShape) override;
};
