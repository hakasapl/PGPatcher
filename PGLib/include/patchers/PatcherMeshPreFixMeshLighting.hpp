#pragma once

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPreFixMeshLighting
 * @brief Pre patcher that caps the soft lighting value of default shader type shapes at softLightingMax
 */
class PatcherMeshPreFixMeshLighting : public PatcherMeshPre {
private:
    constexpr static float softLightingMax = 0.6;

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPre::PatcherMeshPreFactory
     */
    static PatcherMeshPre::PatcherMeshPreFactory factory();

    /**
     * @brief Construct a new Fix Mesh Lighting patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPreFixMeshLighting(std::filesystem::path nifPath,
                                  nifly::NifFile* nif);

    /**
     * @brief Lower the soft lighting of the shape's shader to softLightingMax if it is above
     *
     * @param slots Texture slots of the shape (unused)
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;
};
