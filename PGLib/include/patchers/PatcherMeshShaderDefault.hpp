#pragma once

#include <NifFile.hpp>
#include <filesystem>
#include <string>

#include "patchers/base/PatcherMeshShader.hpp"

/**
 * @class PatcherVanilla
 * @brief Patcher for vanilla
 */
class PatcherMeshShaderDefault : public PatcherMeshShader {
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
    PatcherMeshShaderDefault(std::filesystem::path nifPath, nifly::NifFile* nif);

    /**
     * @brief Check if a shape can be patched by this patcher (without looking at slots)
     *
     * @param nifShape Shape to check
     * @return true Shape can be patched
     * @return false Shape cannot be patched
     */
    auto canApply(nifly::NiShape& nifShape) -> bool override;

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
    auto applyPatch(const NIFUtil::TextureSet& oldSlots, nifly::NiShape& nifShape, const PatcherMatch& match,
        NIFUtil::TextureSet& newSlots) -> bool override;

    /**
     * @brief Apply a match to slots for parallax
     *
     * @param oldSlots Slots to patch
     * @param match Match to apply
     * @return NIFUtil::TextureSet New slots
     */
    auto applyPatchSlots(const NIFUtil::TextureSet& oldSlots, const PatcherMatch& match, NIFUtil::TextureSet& newSlots)
        -> bool override;

    void processNewTXSTRecord(const PatcherMatch& match, const std::string& edid = {}) override;

    /**
     * @brief Apply default shader to a shape (does nothing)
     *
     * @param nifShape Shape to apply shader to
     * @param nifModified Whether the NIF was modified
     */
    auto applyShader(nifly::NiShape& nifShape) -> bool override;
};
