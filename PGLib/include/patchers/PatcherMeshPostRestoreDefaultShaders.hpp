#pragma once

#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPostRestoreDefaultShaders
 * @brief Post patcher that reverts parallax and environment map shaders whose textures do not exist in the load order
 * to the default shader, so pre-patched meshes do not keep a shader without its textures
 */
class PatcherMeshPostRestoreDefaultShaders : public PatcherMeshPost {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPost::PatcherMeshPostFactory
     */
    static PatcherMeshPost::PatcherMeshPostFactory factory();

    /**
     * @brief Construct a new Restore Default Shaders patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPostRestoreDefaultShaders(std::filesystem::path nifPath,
                                         nifly::NifFile* nif);

    /**
     * @brief Restore the default shader when the shape's parallax or complex material textures are missing
     *
     * @param[in,out] slots Texture slots of the shape, the slots of the removed shader are cleared
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;

private:
    /**
     * @brief Restores the default shader on a shape that currently uses the Parallax shader type,
     *        when the referenced parallax texture does not actually exist.
     *
     * @param slots      Texture slot set for the shape (parallax slot is cleared on success).
     * @param shaderProp The BSLightingShaderProperty to inspect and potentially modify.
     * @return true if the shader was restored to default; false if no change was made.
     */
    static bool restoreDefaultShaderFromParallax(PGTypes::TextureSet& slots,
                                                 nifly::BSLightingShaderProperty& shaderProp);

    /**
     * @brief Restores the default shader on a shape that currently uses the environment map shader type, when the
     *        cubemap or the env-mask texture is set but does not exist (the dynamic cubemap always counts as existing).
     *
     * @param slots      Texture slot set for the shape (cubemap and env-mask slots are cleared on success).
     * @param shaderProp The BSLightingShaderProperty to inspect and potentially modify.
     * @return true if the shader was restored to default; false if no change was made.
     */
    static bool restoreDefaultShaderFromComplexMaterial(PGTypes::TextureSet& slots,
                                                        nifly::BSLightingShaderProperty& shaderProp);
};
