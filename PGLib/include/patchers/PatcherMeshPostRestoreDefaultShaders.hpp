#pragma once

#include "patchers/base/PatcherMeshPost.hpp"
#include "util/NIFUtil.hpp"

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
    PatcherMeshPostRestoreDefaultShaders(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Apply this patcher to shape
     *
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    auto applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool override;

private:
    static auto restoreDefaultShaderFromParallax(
        NIFUtil::TextureSet& slots, nifly::BSLightingShaderProperty& shaderProp) -> bool;
    static auto restoreDefaultShaderFromComplexMaterial(
        NIFUtil::TextureSet& slots, nifly::BSLightingShaderProperty& shaderProp) -> bool;
};
