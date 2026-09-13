#pragma once

#include "patchers/base/PatcherTexture.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherTextureGlobal : public PatcherTexture {
public:
    // Type definitions.
    using PatcherGlobalFactory
        = std::function<std::unique_ptr<PatcherTextureGlobal>(std::filesystem::path, DirectX::ScratchImage*)>;
    using PatcherGlobalObject = std::unique_ptr<PatcherTextureGlobal>;

    // Constructors.
    PatcherTextureGlobal(std::filesystem::path texPath,
                         DirectX::ScratchImage* tex,
                         std::string patcherName);
    virtual ~PatcherTextureGlobal() = default;
    PatcherTextureGlobal(const PatcherTextureGlobal& other) = default;
    PatcherTextureGlobal& operator=(const PatcherTextureGlobal& other) = default;
    PatcherTextureGlobal(PatcherTextureGlobal&& other) noexcept = default;
    PatcherTextureGlobal& operator=(PatcherTextureGlobal&& other) noexcept = default;

    /**
     * @brief Apply the patch to the texture if able
     *
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual void applyPatch(bool& ddsModified) = 0;
};
