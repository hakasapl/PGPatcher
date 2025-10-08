#pragma once

#include "patchers/base/PatcherMeshGlobal.hpp"

#include "NifFile.hpp"

#include <filesystem>

class PatcherMeshGlobalFixEffectLightingCS : public PatcherMeshGlobal {
private:
    constexpr static float SOFTLIGHTING_MAX = 0.6F;

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherShaderTransform::PatcherShaderTransformFactory
     */
    static auto getFactory() -> PatcherMeshGlobal::PatcherMeshGlobalFactory;

    /**
     * @brief Construct a new PrePatcher Particle Lights To LP patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshGlobalFixEffectLightingCS(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Apply this patcher to shape
     *
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    auto applyPatch() -> bool override;
};
