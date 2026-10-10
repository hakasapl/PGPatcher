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

    static constexpr const char* shaderName = "ParallaxToCM.hlsl";

    static inline std::shared_mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    /**
     * @brief Forgets every registered texture. Called at the start of a patch run.
     */
    static void reset();

    /**
     * @brief Registers a parallax texture for conversion and maps the output as a generated file. When the previous
     * run's output can be reused (update cache), nothing is generated.
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
    static bool isInProcessList(const std::filesystem::path& texPath);

    /**
     * @brief Computes the output filename for the generated Complex Material texture.
     *
     * @param texPath Path to the source parallax texture.
     * @return Filesystem path with the "_m.dds" suffix for the converted texture.
     */
    static std::filesystem::path outputFilename(const std::filesystem::path& texPath);

    /**
     * @brief Re-applies the texture map side effects of a previously generated output that is being reused
     * (incremental runs) without regenerating the texture.
     *
     * @param texPath Path to the source parallax texture.
     */
    static void replayGenerated(const std::filesystem::path& texPath);

    /**
     * @brief Initializes the DirectX compute shader used for the parallax-to-CM conversion.
     *
     * @return true if the shader was successfully initialized; false otherwise.
     */
    static bool initShader();

    /**
     * @brief Constructs a PatcherTextureHookConvertToCM instance for the given texture.
     *
     * @param ddsPath Path to the DDS texture file.
     * @param dds     Pointer to the loaded ScratchImage for that texture.
     */
    PatcherTextureHookConvertToCM(std::filesystem::path ddsPath,
                                  DirectX::ScratchImage* dds);

    /**
     * @brief Converts the height map to a BC3 complex material texture, writes it to the output folder and adds it to
     * the texture maps.
     *
     * @return true if the patch was successfully applied; false otherwise.
     * @throws std::runtime_error if the patcher has no image.
     */
    bool applyPatch() override;
};
