#pragma once

#include <Geometry.hpp>
#include <NifFile.hpp>

#include "patchers/base/PatcherMesh.hpp"

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherMeshPre : public PatcherMesh {
public:
    // type definitions
    using PatcherMeshPreFactory
        = std::function<std::unique_ptr<PatcherMeshPre>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshPreObject = std::unique_ptr<PatcherMeshPre>;

    // Constructors
    PatcherMeshPre(
        std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave = true);
    virtual ~PatcherMeshPre() = default;
    PatcherMeshPre(const PatcherMeshPre& other) = default;
    auto operator=(const PatcherMeshPre& other) -> PatcherMeshPre& = default;
    PatcherMeshPre(PatcherMeshPre&& other) noexcept = default;
    auto operator=(PatcherMeshPre&& other) noexcept -> PatcherMeshPre& = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @param nifShape Shape to apply patch to
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual auto applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool = 0;
};
