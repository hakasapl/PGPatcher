#pragma once

#include <Geometry.hpp>
#include <NifFile.hpp>

#include "patchers/base/PatcherMesh.hpp"

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherMeshPost : public PatcherMesh {
public:
    // type definitions
    using PatcherMeshPostFactory
        = std::function<std::unique_ptr<PatcherMeshPost>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshPostObject = std::unique_ptr<PatcherMeshPost>;

    // Constructors
    PatcherMeshPost(
        std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave = true);
    virtual ~PatcherMeshPost() = default;
    PatcherMeshPost(const PatcherMeshPost& other) = default;
    auto operator=(const PatcherMeshPost& other) -> PatcherMeshPost& = default;
    PatcherMeshPost(PatcherMeshPost&& other) noexcept = default;
    auto operator=(PatcherMeshPost&& other) noexcept -> PatcherMeshPost& = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @param nifShape Shape to apply patch to
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual auto applyPatch(nifly::NiShape& nifShape) -> bool = 0;
};
