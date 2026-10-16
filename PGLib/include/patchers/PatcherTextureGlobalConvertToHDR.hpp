#pragma once

#include "patchers/base/PatcherTextureGlobal.hpp"

#include <DirectXTex.h>
#include <d3d11.h>
#include <dxgiformat.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <wrl/client.h>

/**
 * @class PatcherTextureGlobalConvertToHDR
 * @brief Global texture patcher that converts a texture to a floating point format, scaling its luminance
 */
class PatcherTextureGlobalConvertToHDR : public PatcherTextureGlobal {
private:
    static inline float s_luminanceMult = 1;
    static inline DXGI_FORMAT s_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    struct ShaderParams {
        float luminanceMult;
    };

public:
    /**
     * @brief Initializes the DirectX compute shader used for the HDR conversion.
     *
     * @return true if the shader was successfully initialized; false otherwise.
     */
    static bool initShader();

    /**
     * @brief Get the Factory object
     *
     * @return PatcherTextureGlobal::PatcherGlobalFactory
     */
    static PatcherTextureGlobal::PatcherGlobalFactory factory();

    /**
     * @brief Loads patcher options from a string key-value map.
     *
     * Recognized keys: "luminance_mult" (float multiplier applied during HDR conversion),
     * "output_format" (DXGI format string for the output texture).
     *
     * @param optionsStr Map of option name strings to value strings.
     */
    static void loadOptions(const std::unordered_map<std::string,
                                                     std::string>& optionsStr);

    /**
     * @brief Construct a new Convert To HDR patcher
     *
     * @param ddsPath dds path to be patched
     * @param dds dds object to be patched
     */
    PatcherTextureGlobalConvertToHDR(std::filesystem::path ddsPath,
                                     DirectX::ScratchImage* dds);

    /**
     * @brief Replace the image with its converted version. The image is left alone if the conversion fails
     *
     * @param[out] ddsModified Set to true when the image was replaced
     */
    void applyPatch(bool& ddsModified) override;
};
