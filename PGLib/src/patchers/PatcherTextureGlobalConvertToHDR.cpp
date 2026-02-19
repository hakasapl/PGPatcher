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

using namespace std;

auto PatcherTextureGlobalConvertToHDR::initShader() -> bool
{
    auto* pgd3d = PGGlobals::getPGD3D();

    if (s_shader != nullptr) {
        return true;
    }

    return pgd3d->initShader(SHADER_NAME, s_shader);
}

auto PatcherTextureGlobalConvertToHDR::getFactory() -> PatcherTextureGlobal::PatcherGlobalFactory
{
    return [](const std::filesystem::path& ddsPath, DirectX::ScratchImage* dds) {
        return std::make_unique<PatcherTextureGlobalConvertToHDR>(ddsPath, dds);
    };
}

void PatcherTextureGlobalConvertToHDR::loadOptions(const unordered_map<string,
                                                                       string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "luminance_mult") {
            s_luminanceMult = stof(value);
        }

        if (option == "output_format") {
            s_outputFormat = PGD3D::getDXGIFormatFromString(value);
        }
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
    auto* pgd3d = PGGlobals::getPGD3D();

    DirectX::ScratchImage newDDS;
    ShaderParams params = {.luminanceMult = s_luminanceMult};
    if (!pgd3d->applyShaderToTexture(
            *getDDS(), newDDS, s_shader, s_outputFormat, 0, 0, &params, sizeof(ShaderParams))) {
        return;
    }

    *getDDS() = std::move(newDDS);
    ddsModified = true;
}
