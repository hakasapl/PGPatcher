#pragma once

#include <filesystem>

#include <shared_mutex>

#include "patchers/base/PatcherTextureHook.hpp"

class PatcherTextureHookConvertToCM : public PatcherTextureHook {
private:
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "ParallaxToCM.hlsl";

    static inline std::shared_mutex s_texToProcessMutex;
    static inline std::unordered_set<std::filesystem::path> s_texToProcess;

public:
    static void addToProcessList(const std::filesystem::path& texPath);
    static auto isInProcessList(const std::filesystem::path& texPath) -> bool;
    static auto getOutputFilename(const std::filesystem::path& texPath) -> std::filesystem::path;

    static auto initShader() -> bool;

    PatcherTextureHookConvertToCM(std::filesystem::path ddsPath, DirectX::ScratchImage* dds);

    auto applyPatch() -> bool override;
};
