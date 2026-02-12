#pragma once

#include "ParallaxGenPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>
#include <vector>

/**
 * @class PatcherMeshShaderVanillaParallax
 * @brief Patcher for vanilla parallax
 */
class PatcherMeshShaderVanillaParallax : public PatcherMeshShader {
private:
    bool m_hasAttachedHavok
        = false; /** Stores at the NIF level whether the NIF has attached havok (incompatible with parallax) */

public:
    /**
     * @brief Get the Factory object for parallax patcher
     *
     * @return PatcherShader::PatcherShaderFactory Factory object
     */
    static auto getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory;

    /**
     * @brief Get the Shader Type for this patcher (Parallax)
     *
     * @return NIFUtil::ShapeShader Parallax
     */
    static auto getShaderType() -> NIFUtil::ShapeShader;

    /**
     * @brief Construct a new Patcher Vanilla Parallax object
     *
     * @param nifPath NIF path to patch
     * @param nif NIF object to patch
     */
    PatcherMeshShaderVanillaParallax(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Check if a shape can be patched by this patcher (without looking at slots)
     *
     * @param nifShape Shape to check
     * @return true Shape can be patched
     * @return false Shape cannot be patched
     */
    auto canApply(nifly::NiShape& nifShape, bool singlepassMATO,
        const ParallaxGenPlugin::ModelRecordType& modelRecordType) -> bool override;

    /**
     * @brief Check if a shape can be patched by this patcher (with slots)
     *
     * @param nifShape Shape to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false No matches found
     */
    auto shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool override;

    /**
     * @brief Check if slots can accomodate parallax
     *
     * @param oldSlots Slots to check
     * @param[out] matches Matches found
     * @return true Found matches
     * @return false No matches found
     */
    auto shouldApply(const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches) -> bool override;

    /**
     * @brief Apply a match to a shape for parallax
     *
     * @param nifShape Shape to patch
     * @param match Match to apply
     * @param[out] nifModified Whether the NIF was modified
     * @param[out] shapeDeleted Whether the shape was deleted (always false)
     * @return NIFUtil::TextureSet New slots of shape
     */
    void applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape, const PatcherMatch& match) override;

    /**
     * @brief Apply a match to slots for parallax
     *
     * @param oldSlots Slots to patch
     * @param match Match to apply
     * @return NIFUtil::TextureSet New slots
     */
    void applyPatchSlots(NIFUtil::TextureSet& slots, const PatcherMatch& match) override;

    /**
     * @brief Apply parallax shader to a shape
     *
     * @param nifShape Shape to apply shader to
     * @param nifModified Whether the NIF was modified
     */
    void applyShader(nifly::NiShape& nifShape) override;
};
