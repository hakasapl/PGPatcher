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
 * @class PatcherMeshPre
 * @brief Base class for pre patchers, which run on every patchable shape before its shader patcher
 */
class PatcherMeshPre : public PatcherMesh {
public:
    // Type definitions.
    using PatcherMeshPreFactory
        = std::function<std::unique_ptr<PatcherMeshPre>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshPreObject = std::unique_ptr<PatcherMeshPre>;

    // Constructors.
    PatcherMeshPre(std::filesystem::path nifPath,
                   nifly::NifFile* nif,
                   std::string patcherName);
    virtual ~PatcherMeshPre() = default;
    PatcherMeshPre(const PatcherMeshPre& other) = default;
    PatcherMeshPre& operator=(const PatcherMeshPre& other) = default;
    PatcherMeshPre(PatcherMeshPre&& other) noexcept = default;
    PatcherMeshPre& operator=(PatcherMeshPre&& other) noexcept = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @param[in,out] slots Texture slots of the shape as the patchers see them (written to the NIF afterwards)
     * @param nifShape Shape to apply patch to
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual bool applyPatch(PGTypes::TextureSet& slots,
                            nifly::NiShape& nifShape) = 0;
};
