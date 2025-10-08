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
 * @class PrePatcherParticleLightsToLP
 * @brief patcher to transform particle lights to LP
 */
class PatcherTextureGlobalConvertToHDR : public PatcherTextureGlobal {
private:
    static inline float s_luminanceMult = 1.0F;
    static inline DXGI_FORMAT s_outputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "ParallaxToCM.hlsl";

    struct ShaderParams {
        float luminanceMult;
    };

public:
    static auto initShader() -> bool;

    /**
     * @brief Get the Factory object
     *
     * @return PatcherShaderTransform::PatcherShaderTransformFactory
     */
    static auto getFactory() -> PatcherTextureGlobal::PatcherGlobalFactory;

    static void loadOptions(const std::unordered_map<std::string, std::string>& optionsStr);

    /**
     * @brief Construct a new PrePatcher Particle Lights To LP patcher
     *
     * @param ddsPath dds path to be patched
     * @param dds dds object to be patched
     */
    PatcherTextureGlobalConvertToHDR(std::filesystem::path ddsPath, DirectX::ScratchImage* dds);

    /**
     * @brief Apply this patcher to DDS
     *
     * @return true DDS was patched
     * @return false DDS was not patched
     */
    void applyPatch(bool& ddsModified) override;
};
