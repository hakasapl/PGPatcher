#pragma once

#include <filesystem>

#include "patchers/base/PatcherTextureHook.hpp"

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

    static inline std::mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    static void addToProcessList(const std::filesystem::path& texPath);
    static auto isInProcessList(const std::filesystem::path& texPath) -> bool;
    static auto getOutputFilename(const std::filesystem::path& texPath) -> std::filesystem::path;

    static auto initShader() -> bool;

    PatcherTextureHookFixSSS(std::filesystem::path ddsPath, DirectX::ScratchImage* dds);

    auto applyPatch() -> bool override;
};
