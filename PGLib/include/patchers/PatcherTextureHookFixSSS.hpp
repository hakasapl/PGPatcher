#pragma once

#include "patchers/base/PatcherTextureHook.hpp"

#include <DirectXTex.h>
#include <d3d11.h>

#include <filesystem>
#include <shared_mutex>
#include <unordered_set>
#include <wrl/client.h>

/**
 * @brief Texture hook patcher that fixes SubSurface Scattering (SSS) textures by
 *        de-lighting and downscaling the diffuse map into a subsurface color texture
 *        using a compute shader.
 */
class PatcherTextureHookFixSSS : public PatcherTextureHook {
private:
    static constexpr const float SHADER_ALBEDO_SAT_POWER = 0.5F;
    static constexpr const float SHADER_ALBEDO_NORM = 1.8F;

    struct ShaderParams {
        float fAlbedoSatPower;
        float fAlbedoNorm;
    };
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "SSSFix.hlsl";

    static inline std::shared_mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    /**
     * @brief Registers a diffuse texture for SSS fixing and tracks the generated output file.
     *
     * @param texPath Path to the diffuse texture to be processed.
     */
    static void addToProcessList(const std::filesystem::path& texPath);

    /**
     * @brief Checks whether a diffuse texture has already been registered for SSS fixing.
     *
     * @param texPath Path to the diffuse texture to check.
     * @return true if the texture is in the process list; false otherwise.
     */
    static auto isInProcessList(const std::filesystem::path& texPath) -> bool;

    /**
     * @brief Computes the output filename for the generated subsurface color texture.
     *
     * @param texPath Path to the source diffuse texture.
     * @return Filesystem path with the "_s.dds" suffix for the fixed SSS texture.
     */
    static auto getOutputFilename(const std::filesystem::path& texPath) -> std::filesystem::path;

    /**
     * @brief Initializes the DirectX compute shader used for the SSS fix.
     *
     * @return true if the shader was successfully initialized; false otherwise.
     */
    static auto initShader() -> bool;

    /**
     * @brief Constructs a PatcherTextureHookFixSSS instance for the given texture.
     *
     * @param ddsPath Path to the DDS texture file.
     * @param dds     Pointer to the loaded ScratchImage for that texture.
     */
    PatcherTextureHookFixSSS(std::filesystem::path ddsPath,
                             DirectX::ScratchImage* dds);

    /**
     * @brief Applies the SSS fix shader and writes the output subsurface color DDS.
     *
     * @return true if the patch was successfully applied; false otherwise.
     */
    auto applyPatch() -> bool override;
};
