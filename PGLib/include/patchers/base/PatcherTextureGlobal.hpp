#pragma once

#include "patchers/base/PatcherTexture.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

/**
 * @class PatcherTextureGlobal
 * @brief Base class for global texture patchers, which run on every texture of the load order and change it in place
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
     * @param[out] ddsModified Set to true when the texture changed (left alone otherwise)
     */
    virtual void applyPatch(bool& ddsModified) = 0;
};
