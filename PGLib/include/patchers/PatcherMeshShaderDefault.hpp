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
 * @class PatcherMeshShaderDefault
 * @brief Shader patcher for the default (unpatched) shader
 *
 * It never changes a shape. Its matches are the shape's own diffuse and normal textures, so the mods that provide them
 * take part in the mod conflict resolution and win when they have the higher priority.
 */
class PatcherMeshShaderDefault : public PatcherMeshShader {
public:
    /**
     * @brief Get the Factory object for this patcher
     *
     * @return PatcherMeshShader::PatcherMeshShaderFactory Factory object
     */
    static PatcherMeshShader::PatcherMeshShaderFactory factory();

    /**
     * @brief Get the Shader Type for this patcher
     *
     * @return PGEnums::ShapeShader None
     */
    static PGEnums::ShapeShader shaderType();

    /**
     * @brief Construct a new default shader patcher
     *
     * @param nifPath NIF path to patch
     * @param nif NIF object to patch
     */
    PatcherMeshShaderDefault(std::filesystem::path nifPath,
                             nifly::NifFile* nif);

    /**
     * @brief Every shape can keep its shader
     *
     * @return true always
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
     * @brief Match the diffuse and normal textures of the slots that exist in the load order
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches found, one per existing texture
     * @return true Found matches
     * @return false No matches found
     */
    bool shouldApply(const PGTypes::TextureSet& oldSlots,
                     std::vector<PatcherMatch>& matches) override;

    /**
     * @brief Does nothing
     */
    void applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape,
                    const PatcherMatch& match) override;

    /**
     * @brief Does nothing
     */
    void applyPatchSlots(PGTypes::TextureSet& slots,
                         const PatcherMatch& match) override;

    /**
     * @brief Does nothing
     */
    void applyShader(nifly::NiShape& nifShape) override;
};
