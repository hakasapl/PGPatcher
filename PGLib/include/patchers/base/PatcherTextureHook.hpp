#pragma once

#include "patchers/base/PatcherTexture.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherTextureHook : public PatcherTexture {
protected:
    static inline std::mutex s_generatedFileTrackerMutex;

public:
    // Type definitions.
    using PatcherGlobalFactory
        = std::function<std::unique_ptr<PatcherTextureHook>(std::filesystem::path, DirectX::ScratchImage*)>;
    using PatcherGlobalObject = std::unique_ptr<PatcherTextureHook>;

    // Constructors.
    PatcherTextureHook(std::filesystem::path texPath,
                       DirectX::ScratchImage* tex,
                       std::string patcherName);
    virtual ~PatcherTextureHook() = default;
    PatcherTextureHook(const PatcherTextureHook& other) = delete;
    PatcherTextureHook& operator=(const PatcherTextureHook& other) = delete;
    PatcherTextureHook(PatcherTextureHook&& other) noexcept = default;
    PatcherTextureHook& operator=(PatcherTextureHook&& other) noexcept = default;

    /**
     * @brief Apply the patch to the texture if able
     *
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual bool applyPatch() = 0;
};
