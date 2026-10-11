#pragma once

#include "patchers/base/PatcherTexture.hpp"

#include <DirectXTex.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

/**
 * @class PatcherTextureHook
 * @brief Base class for texture hooks: patchers that mesh patchers register a texture for, and that generate a new
 * texture from it during the texture phase
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
     * @brief Generate the output texture from the hooked texture and write it to the output folder
     *
     * @return true Output was written
     * @return false Generation or writing failed
     */
    virtual bool applyPatch() = 0;
};
