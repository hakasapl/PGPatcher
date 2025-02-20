#pragma once

#include <DirectXTex.h>

#include "patchers/base/PatcherTexture.hpp"

/**
 * @class PrePatcher
 * @brief Base class for prepatchers
 */
class PatcherTextureHook : public PatcherTexture {
private:
    // Instance vars
    DirectX::ScratchImage m_ddsImage;

protected:
    static inline std::mutex s_generatedFileTrackerMutex;

public:
    // type definitions
    using PatcherGlobalFactory
        = std::function<std::unique_ptr<PatcherTextureHook>(std::filesystem::path, DirectX::ScratchImage*)>;
    using PatcherGlobalObject = std::unique_ptr<PatcherTextureHook>;

    // Constructors
    PatcherTextureHook(std::filesystem::path texPath, std::string patcherName);
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
    virtual auto applyPatch(std::filesystem::path& newPath) -> bool = 0;
};
