#pragma once

#include "patchers/base/PatcherTextureHook.hpp"

#include <DirectXTex.h>
#include <d3d11.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <unordered_set>
#include <vector>
#include <wrl/client.h>

/**
 * @brief Texture hook patcher that fixes SubSurface Scattering (SSS) textures by
 *        de-lighting and downscaling the diffuse map into a subsurface color texture
 *        using a compute shader.
 */
class PatcherTextureHookFixSSS : public PatcherTextureHook {
private:
    static constexpr const float shaderAlbedoSatPower = 0.5;
    static constexpr const float shaderAlbedoNorm = 1.8;
    /// @brief The output has half the width and height of the diffuse.
    static constexpr size_t scaleFactor = 2;
    /// @brief The format the shader writes and the format and compression of the output file, which the generator
    /// signature covers.
    static constexpr DXGI_FORMAT shaderOutputFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    static constexpr DXGI_FORMAT outputFormat = DXGI_FORMAT_BC2_UNORM;
    static constexpr DirectX::TEX_COMPRESS_FLAGS compressFlags = DirectX::TEX_COMPRESS_DEFAULT;

    struct ShaderParams {
        float fAlbedoSatPower;
        float fAlbedoNorm;
    };
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static inline std::shared_mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    /// @brief The patcher name of the hook, as its log lines carry it.
    static constexpr const char* hookName = "SSSFix";

    /**
     * @brief Forgets every registered texture. Called at the start of a patch run.
     */
    static void reset();

    /**
     * @brief Registers a diffuse texture for SSS fixing and maps the output as a generated file. When the previous
     * run's output can be reused (update cache), nothing is generated.
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
    static bool isInProcessList(const std::filesystem::path& texPath);

    /**
     * @brief The textures registered for SSS fixing, in no particular order.
     */
    static std::vector<std::filesystem::path> processList();

    /**
     * @brief The signature of the fix (the shader, the formats, the scale and the shader parameters), see
     * PatcherTextureHook::signatureOf(). A cached output is only valid for the signature it was generated with.
     */
    static std::uint32_t generatorSignature();

    /**
     * @brief Computes the output filename for the generated subsurface color texture.
     *
     * @param texPath Path to the source diffuse texture.
     * @return Filesystem path with the "_s.dds" suffix for the fixed SSS texture.
     */
    static std::filesystem::path outputFilename(const std::filesystem::path& texPath);

    /**
     * @brief Records the texture type of a previously generated output that is being reused (incremental runs)
     * without regenerating the texture. Generated maps are never added to the texture maps.
     *
     * @param texPath Path to the source diffuse texture.
     */
    static void replayGenerated(const std::filesystem::path& texPath);

    /**
     * @brief Initializes the DirectX compute shader used for the SSS fix.
     *
     * @return true if the shader was successfully initialized; false otherwise.
     */
    static bool initShader();

    /**
     * @brief Constructs a PatcherTextureHookFixSSS instance for the given texture.
     *
     * @param ddsPath Path to the DDS texture file.
     * @param dds     Pointer to the loaded ScratchImage for that texture.
     */
    PatcherTextureHookFixSSS(std::filesystem::path ddsPath,
                             DirectX::ScratchImage* dds);

    /**
     * @brief Generates the half size BC2 subsurface color texture, writes it to the output folder and adds it to the
     * texture maps.
     *
     * @return true if the patch was successfully applied; false otherwise.
     * @throws std::runtime_error if the patcher has no image.
     */
    bool applyPatch() override;

    /**
     * @brief Generates the half size BC2 subsurface color texture in memory.
     *
     * @param[out] compressed The subsurface color texture.
     * @return true if the texture was generated; false otherwise.
     * @throws std::runtime_error if the patcher has no image.
     */
    bool generate(DirectX::ScratchImage& compressed) override;
};
