#pragma once

#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGNIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>

class PatcherMeshPostRestoreDefaultShaders : public PatcherMeshPost {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherShaderTransform::PatcherShaderTransformFactory
     */
    static auto getFactory() -> PatcherMeshPost::PatcherMeshPostFactory;

    /**
     * @brief Construct a new PrePatcher Particle Lights To LP patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPostRestoreDefaultShaders(std::filesystem::path nifPath,
                                         nifly::NifFile* nif);

    /**
     * @brief Apply this patcher to shape
     *
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    auto applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) -> bool override;

private:
    /**
     * @brief Restores the default shader on a shape that currently uses the Parallax shader type,
     *        when the referenced parallax texture does not actually exist.
     *
     * @param slots      Texture slot set for the shape (parallax slot is cleared on success).
     * @param shaderProp The BSLightingShaderProperty to inspect and potentially modify.
     * @return true if the shader was restored to default; false if no change was made.
     */
    static auto restoreDefaultShaderFromParallax(PGTypes::TextureSet& slots,
                                                 nifly::BSLightingShaderProperty& shaderProp) -> bool;

    /**
     * @brief Restores the default shader on a shape that currently uses the Complex Material (env-map)
     *        shader type, when neither the cubemap nor the env-mask texture exists.
     *
     * @param slots      Texture slot set for the shape (cubemap and env-mask slots are cleared on success).
     * @param shaderProp The BSLightingShaderProperty to inspect and potentially modify.
     * @return true if the shader was restored to default; false if no change was made.
     */
    static auto restoreDefaultShaderFromComplexMaterial(PGTypes::TextureSet& slots,
                                                        nifly::BSLightingShaderProperty& shaderProp) -> bool;
};
