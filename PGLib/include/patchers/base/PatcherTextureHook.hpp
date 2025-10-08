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
    // type definitions
    using PatcherGlobalFactory
        = std::function<std::unique_ptr<PatcherTextureHook>(std::filesystem::path, DirectX::ScratchImage*)>;
    using PatcherGlobalObject = std::unique_ptr<PatcherTextureHook>;

    // Constructors
    PatcherTextureHook(std::filesystem::path texPath, DirectX::ScratchImage* tex, std::string patcherName);
    virtual ~PatcherTextureHook() = default;
    PatcherTextureHook(const PatcherTextureHook& other) = delete;
    auto operator=(const PatcherTextureHook& other) -> PatcherTextureHook& = delete;
    PatcherTextureHook(PatcherTextureHook&& other) noexcept = default;
    auto operator=(PatcherTextureHook&& other) noexcept -> PatcherTextureHook& = default;

    /**
     * @brief Apply the patch to the texture if able
     *
     * @return true Patch was applied
     * @return false Patch was not applied
     */
    virtual auto applyPatch() -> bool = 0;
};
