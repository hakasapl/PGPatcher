#pragma once

#include "patchers/base/PatcherMesh.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherMeshGlobal : public PatcherMesh {
public:
    // Type definitions.
    using PatcherMeshGlobalFactory
        = std::function<std::unique_ptr<PatcherMeshGlobal>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshGlobalObject = std::unique_ptr<PatcherMeshGlobal>;

    // Constructors.
    PatcherMeshGlobal(std::filesystem::path nifPath,
                      nifly::NifFile* nif,
                      std::string patcherName);
    virtual ~PatcherMeshGlobal() = default;
    PatcherMeshGlobal(const PatcherMeshGlobal& other) = default;
    PatcherMeshGlobal& operator=(const PatcherMeshGlobal& other) = default;
    PatcherMeshGlobal(PatcherMeshGlobal&& other) noexcept = default;
    PatcherMeshGlobal& operator=(PatcherMeshGlobal&& other) noexcept = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual bool applyPatch() = 0;
};
