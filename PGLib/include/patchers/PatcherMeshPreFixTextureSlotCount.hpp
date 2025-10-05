#pragma once

#include "patchers/base/PatcherMeshPre.hpp"

class PatcherMeshPreFixTextureSlotCount : public PatcherMeshPre {
private:
    constexpr static int SLOT_COUNT = 9;

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherShaderTransform::PatcherShaderTransformFactory
     */
    static auto getFactory() -> PatcherMeshPre::PatcherMeshPreFactory;

    /**
     * @brief Construct a new PrePatcher Particle Lights To LP patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPreFixTextureSlotCount(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Apply this patcher to shape
     *
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    auto applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool override;
};
