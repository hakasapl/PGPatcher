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
    // type definitions
    using PatcherMeshGlobalFactory
        = std::function<std::unique_ptr<PatcherMeshGlobal>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshGlobalObject = std::unique_ptr<PatcherMeshGlobal>;

    // Constructors
    PatcherMeshGlobal(
        std::filesystem::path nifPath, nifly::NifFile* nif, std::string patcherName, const bool& triggerSave = true);
    virtual ~PatcherMeshGlobal() = default;
    PatcherMeshGlobal(const PatcherMeshGlobal& other) = default;
    auto operator=(const PatcherMeshGlobal& other) -> PatcherMeshGlobal& = default;
    PatcherMeshGlobal(PatcherMeshGlobal&& other) noexcept = default;
    auto operator=(PatcherMeshGlobal&& other) noexcept -> PatcherMeshGlobal& = default;

    /**
     * @brief Apply the patch to the NIFShape if able
     *
     * @param nifShape Shape to apply patch to
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual auto applyPatch() -> bool = 0;
};
