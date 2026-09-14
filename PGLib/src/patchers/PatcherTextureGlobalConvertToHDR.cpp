#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"

#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "patchers/base/PatcherTextureGlobal.hpp"
#include <DirectXTex.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

bool PatcherTextureGlobalConvertToHDR::initShader()
{
    auto* pgd3d = PGGlobals::pgD3D();

    if (s_shader)
        return true;

    return pgd3d->initShader(shaderName, s_shader);
}

auto PatcherTextureGlobalConvertToHDR::factory() -> PatcherTextureGlobal::PatcherGlobalFactory
{
    return [](const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds) {
        return std::make_unique<PatcherTextureGlobalConvertToHDR>(ddsPath, dds);
    };
}

void PatcherTextureGlobalConvertToHDR::loadOptions(const std::unordered_map<std::string,
                                                                            std::string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "luminance_mult")
            s_luminanceMult = std::stof(value);

        if (option == "output_format")
            s_outputFormat = PGD3D::dxgiFormatFromString(value);
    }
}

PatcherTextureGlobalConvertToHDR::PatcherTextureGlobalConvertToHDR(std::filesystem::path ddsPath,
                                                                   DirectX::ScratchImage* dds)
    : PatcherTextureGlobal(std::move(ddsPath),
                           dds,
                           "ConvertToHDR")
{
}

void PatcherTextureGlobalConvertToHDR::applyPatch(bool& ddsModified)
{
    auto* pgd3d = PGGlobals::pgD3D();

    DirectX::ScratchImage newDDS;
    ShaderParams params = { .luminanceMult = s_luminanceMult };
    if (!pgd3d->applyShaderToTexture(*dds(), newDDS, s_shader, s_outputFormat, 0, 0, &params, sizeof(ShaderParams)))
        return;

    *dds() = std::move(newDDS);
    ddsModified = true;
}
