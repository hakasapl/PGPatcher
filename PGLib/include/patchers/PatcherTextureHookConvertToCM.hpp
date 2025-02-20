#pragma once

#include <filesystem>

#include "patchers/base/PatcherTextureHook.hpp"

class PatcherTextureHookConvertToCM : PatcherTextureHook {
private:
    static inline Microsoft::WRL::ComPtr<ID3D11ComputeShader> s_shader;

    static constexpr const char* SHADER_NAME = "ParallaxToCM.hlsl";

public:
    static auto initShader() -> bool;

    PatcherTextureHookConvertToCM(std::filesystem::path texPath);

    auto applyPatch(std::filesystem::path& newPath) -> bool override;
};
