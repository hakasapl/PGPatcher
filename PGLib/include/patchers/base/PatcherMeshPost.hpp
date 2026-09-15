#pragma once

#include "patchers/base/PatcherMesh.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherMeshPost : public PatcherMesh {
public:
    // Type definitions.
    using PatcherMeshPostFactory
        = std::function<std::unique_ptr<PatcherMeshPost>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshPostObject = std::unique_ptr<PatcherMeshPost>;

    // Constructors.
    PatcherMeshPost(std::filesystem::path nifPath,
                    nifly::NifFile* nif,
                    std::string patcherName);
    virtual ~PatcherMeshPost() = default;
    PatcherMeshPost(const PatcherMeshPost& other) = default;
    PatcherMeshPost& operator=(const PatcherMeshPost& other) = default;
    PatcherMeshPost(PatcherMeshPost&& other) noexcept = default;
    PatcherMeshPost& operator=(PatcherMeshPost&& other) noexcept = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @param nifShape Shape to apply patch to
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual bool applyPatch(PGTypes::TextureSet& slots,
                            nifly::NiShape& nifShape) = 0;
};
