#pragma once

#include <filesystem>

#include "patchers/base/PatcherTextureHook.hpp"

class PatcherTextureHookFixSSS : PatcherTextureHook {
private:
    static constexpr const float SHADER_ALBEDO_SAT_POWER = 0.5F;
    static constexpr const float SHADER_ALBEDO_NORM = 1.8F;

    struct ShaderParams {
        float fAlbedoSatPower;
        float fAlbedoNorm;
    };
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "SSSFix.hlsl";

public:
    static auto initShader() -> bool;

    PatcherTextureHookFixSSS(std::filesystem::path texPath);

    auto applyPatch(std::filesystem::path& newPath) -> bool override;
};
