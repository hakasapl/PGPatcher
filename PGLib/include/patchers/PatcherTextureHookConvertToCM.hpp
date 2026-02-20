#pragma once

#include "patchers/base/PatcherTextureHook.hpp"

#include <DirectXTex.h>
#include <d3d11.h>

#include <filesystem>
#include <shared_mutex>
#include <unordered_set>
#include <wrl/client.h>

/**
 * @brief Texture hook patcher that converts parallax height-map textures into
 *        Complex Material (CM) env-mask textures by running a compute shader.
 */
class PatcherTextureHookConvertToCM : public PatcherTextureHook {
private:
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "ParallaxToCM.hlsl";

    static inline std::shared_mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    /**
     * @brief Registers a parallax texture for conversion and tracks the generated output file.
     *
     * @param texPath Path to the parallax texture to be converted.
     */
    static void addToProcessList(const std::filesystem::path& texPath);

    /**
     * @brief Checks whether a parallax texture has already been registered for conversion.
     *
     * @param texPath Path to the parallax texture to check.
     * @return true if the texture is in the process list; false otherwise.
     */
    static auto isInProcessList(const std::filesystem::path& texPath) -> bool;

    /**
     * @brief Computes the output filename for the generated Complex Material texture.
     *
     * @param texPath Path to the source parallax texture.
     * @return Filesystem path with the "_m.dds" suffix for the converted texture.
     */
    static auto getOutputFilename(const std::filesystem::path& texPath) -> std::filesystem::path;

    /**
     * @brief Initializes the DirectX compute shader used for the parallax-to-CM conversion.
     *
     * @return true if the shader was successfully initialized; false otherwise.
     */
    static auto initShader() -> bool;

    /**
     * @brief Constructs a PatcherTextureHookConvertToCM instance for the given texture.
     *
     * @param ddsPath Path to the DDS texture file.
     * @param dds     Pointer to the loaded ScratchImage for that texture.
     */
    PatcherTextureHookConvertToCM(std::filesystem::path ddsPath,
                                  DirectX::ScratchImage* dds);

    /**
     * @brief Applies the parallax-to-Complex-Material conversion and writes the output DDS.
     *
     * @return true if the patch was successfully applied; false otherwise.
     */
    auto applyPatch() -> bool override;
};
