#pragma once

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPreFixTextureSlotCount
 * @brief Pre patcher that grows texture sets with fewer than slotCount slots, so every slot a shader patcher writes
 * exists
 */
class PatcherMeshPreFixTextureSlotCount : public PatcherMeshPre {
private:
    constexpr static int slotCount = 9;

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPre::PatcherMeshPreFactory
     */
    static PatcherMeshPre::PatcherMeshPreFactory factory();

    /**
     * @brief Construct a new Fix Texture Slot Count patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPreFixTextureSlotCount(std::filesystem::path nifPath,
                                      nifly::NifFile* nif);

    /**
     * @brief Resize the shape's texture set to slotCount slots if it has fewer
     *
     * @param slots Texture slots of the shape (unused)
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;
};
