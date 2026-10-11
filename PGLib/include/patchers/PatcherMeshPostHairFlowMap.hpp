#pragma once

#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshPostHairFlowMap
 * @brief Post patcher that assigns a hair flow map (a "_flow" texture matching the normal map) to hair shapes
 */
class PatcherMeshPostHairFlowMap : public PatcherMeshPost {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshPost::PatcherMeshPostFactory
     */
    static PatcherMeshPost::PatcherMeshPostFactory factory();

    /**
     * @brief Construct a new Hair Flow Map patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshPostHairFlowMap(std::filesystem::path nifPath,
                               nifly::NifFile* nif);

    /**
     * @brief Put the flow map matching the normal map into the backlight slot and set the back lighting flag
     *
     * Applies to hair tint shapes without back lighting whose normal map exists and has a flow map in the load order.
     *
     * @param[in,out] slots Texture slots of the shape
     * @param nifShape Shape to patch
     * @return true Shape was patched
     * @return false Shape was not patched
     */
    bool applyPatch(PGTypes::TextureSet& slots,
                    nifly::NiShape& nifShape) override;
};
