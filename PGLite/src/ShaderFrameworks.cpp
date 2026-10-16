#include "PCH.h"

#include "ShaderFrameworks.hpp"

#include "util/Logger.hpp"

#include <cstddef>
#include <vector>
#include <windows.h>

#include <psapi.h>

namespace PGLite {

namespace {

constexpr const wchar_t* communityShadersDLL = L"CommunityShaders.dll";
constexpr const char* enbExport = "ENBGetSDKVersion";

/// @brief Whether a loaded module exports the ENB SDK entry point. ENB's proxy is d3d11.dll as a rule, but the export
/// is what identifies it whatever the file name, which is how TerrainHelper looks for it.
bool isENBLoaded()
{
    std::vector<HMODULE> modules(256);
    DWORD needed = 0;
    while (true) {
        const auto size = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
        if (!K32EnumProcessModules(GetCurrentProcess(), modules.data(), size, &needed))
            return false;

        if (needed <= size)
            break;

        modules.resize(needed / sizeof(HMODULE));
    }

    const auto count = needed / sizeof(HMODULE);
    for (std::size_t i = 0; i < count; i++)
        if (GetProcAddress(modules.at(i), enbExport))
            return true;

    return false;
}

}

ShaderFrameworks::Detected ShaderFrameworks::detect()
{
    return { .hasCommunityShaders = GetModuleHandleW(communityShadersDLL) != nullptr, .hasENB = isENBLoaded() };
}

void ShaderFrameworks::limitPatchers(PGParams& params,
                                     const Detected& detected)
{
    if (!detected.hasCommunityShaders) {
        if (params.shaderPatcher.isTruePBREnabled) {
            Logger::warn("TruePBR is off: it needs Community Shaders");
            params.shaderPatcher.isTruePBREnabled = false;
        }
        if (params.postPatcher.isHairFlowMapEnabled) {
            Logger::warn("The hair flow map is off: it needs Community Shaders");
            params.postPatcher.isHairFlowMapEnabled = false;
        }
    }

    if (!detected.hasENB && params.prePatcher.isFixMeshLightingEnabled) {
        Logger::warn("Fix Mesh Lighting is off: it needs ENB");
        params.prePatcher.isFixMeshLightingEnabled = false;
    }
}

PGParams ShaderFrameworks::firstStartParams(const Detected& detected)
{
    auto params = PGParams::defaults();
    params.prePatcher.isFixMeshLightingEnabled = detected.hasENB;
    params.shaderPatcher.isParallaxEnabled = true;
    params.shaderPatcher.isComplexMaterialEnabled = true;
    params.shaderPatcher.isTruePBREnabled = detected.hasCommunityShaders;
    params.shaderTransforms.isParallaxToCMEnabled = false;
    params.postPatcher.disablePrePatchedMaterials = true;
    params.postPatcher.isFixSSSEnabled = false;
    params.postPatcher.isHairFlowMapEnabled = false;
    return params;
}

}
