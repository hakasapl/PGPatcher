#pragma once

#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPostFixSSS
 * @brief Post patcher that gives soft lighting shapes whose glow slot repeats the diffuse texture a dedicated
 * subsurface texture, generated from the diffuse by PatcherTextureHookFixSSS
 */
class PatcherMeshPostFixSSS : public PatcherMeshPost {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPost::PatcherMeshPostFactory
     */
    static PatcherMeshPost::PatcherMeshPostFactory factory();

    /**
     * @brief Construct a new Fix SSS patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPostFixSSS(std::filesystem::path nifPath,
                          nifly::NifFile* nif);

    /**
     * @brief Register the diffuse texture for SSS generation and point the glow slot at the generated texture
     *
     * Applies to default shader type shapes with soft lighting whose diffuse and glow slots hold the same DDS.
     *
     * @param[in,out] slots Texture slots of the shape
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;
};
