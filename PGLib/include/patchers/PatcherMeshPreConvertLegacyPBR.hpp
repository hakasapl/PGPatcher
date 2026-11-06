#pragma once

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPreConvertLegacyPBR
 * @brief Pre patcher that converts True PBR shapes from the legacy format to the standalone format
 *
 * Community Shaders 2.0 no longer reads the legacy format, so without this a PBR mesh a mod ships in it would lose its
 * PBR material. There is no option for it: it is always registered, with or without the True PBR patcher, and it does
 * nothing unless PatcherMeshShaderTruePBR::format() is the standalone format. It has to run before the patchers that
 * read the legacy material fields as what they are in vanilla, so right after the stock marker.
 */
class PatcherMeshPreConvertLegacyPBR : public PatcherMeshPre {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPre::PatcherMeshPreFactory
     */
    static PatcherMeshPre::PatcherMeshPreFactory factory();

    /**
     * @brief Construct a new Convert Legacy PBR patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPreConvertLegacyPBR(std::filesystem::path nifPath,
                                   nifly::NifFile* nif);

    /**
     * @brief Convert the shape if it has the legacy PBR flag, see PatcherMeshShaderTruePBR::convertLegacyShape()
     *
     * @param[in,out] slots Texture slots of the shape
     * @param nifShape Shape to patch
     * @return true Shape was converted
     * @return false Shape was not converted
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;
};
