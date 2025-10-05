#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"
#include "ParallaxGenD3D.hpp"

#include <DirectXTex.h>

using namespace std;

auto PatcherTextureGlobalConvertToHDR::initShader() -> bool
{
    if (s_shader != nullptr) {
        return true;
    }

    return getPGD3D()->initShader(SHADER_NAME, s_shader);
}

auto PatcherTextureGlobalConvertToHDR::getFactory() -> PatcherTextureGlobal::PatcherGlobalFactory
{
    return [](const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds) {
        return std::make_unique<PatcherTextureGlobalConvertToHDR>(ddsPath, dds);
    };
}

void PatcherTextureGlobalConvertToHDR::loadOptions(const unordered_map<string, string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "luminance_mult") {
            s_luminanceMult = stof(value);
        }

        if (option == "output_format") {
            s_outputFormat = ParallaxGenD3D::getDXGIFormatFromString(value);
        }
    }
}

PatcherTextureGlobalConvertToHDR::PatcherTextureGlobalConvertToHDR(
    std::filesystem::path ddsPath, DirectX::ScratchImage* dds)
    : PatcherTextureGlobal(std::move(ddsPath), dds, "ConvertToHDR")
{
}

void PatcherTextureGlobalConvertToHDR::applyPatch(bool& ddsModified)
{
    DirectX::ScratchImage newDDS;
    ShaderParams params = { .luminanceMult = s_luminanceMult };
    if (!getPGD3D()->applyShaderToTexture(
            *getDDS(), newDDS, s_shader, s_outputFormat, 0, 0, &params, sizeof(ShaderParams))) {
        return;
    }

    *getDDS() = std::move(newDDS);
    ddsModified = true;
}
