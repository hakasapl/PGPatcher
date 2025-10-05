#include "patchers/PatcherTextureHookFixSSS.hpp"
#include <DirectXTex.h>
#include <dxgiformat.h>

using namespace std;
using namespace Microsoft::WRL;

auto PatcherTextureHookFixSSS::addToProcessList(const filesystem::path& texPath) -> void
{
    const unique_lock lock(s_texToProcessMutex);
    s_texToProcess.insert(texPath);
}

auto PatcherTextureHookFixSSS::isInProcessList(const filesystem::path& texPath) -> bool
{
    const shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

auto PatcherTextureHookFixSSS::getOutputFilename(const filesystem::path& texPath) -> filesystem::path
{
    const auto texBase = NIFUtil::getTexBase(texPath, NIFUtil::TextureSlots::DIFFUSE);
    return texBase + L"_s.dds";
}

auto PatcherTextureHookFixSSS::initShader() -> bool
{
    if (s_shader != nullptr) {
        return true;
    }

    return getPGD3D()->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookFixSSS::PatcherTextureHookFixSSS(std::filesystem::path ddsPath, DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath), dds, "SSSFix")
{
}

auto PatcherTextureHookFixSSS::applyPatch() -> bool
{
    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = NIFUtil::getTexBase(getDDSPath(), NIFUtil::TextureSlots::DIFFUSE);
    const auto newPath = texBase + L"_s.dds";

    if (getPGD()->isGenerated(newPath)) {
        // already generated
        return true;
    }

    DirectX::ScratchImage newDDS;
    const auto newWidth = static_cast<UINT>(getDDS()->GetMetadata().width / 2);
    const auto newHeight = static_cast<UINT>(getDDS()->GetMetadata().height / 2);
    // the shader delights and also reduces size by 4 for efficiency
    ShaderParams params = { .fAlbedoSatPower = SHADER_ALBEDO_SAT_POWER, .fAlbedoNorm = SHADER_ALBEDO_NORM };
    if (!getPGD3D()->applyShaderToTexture(*getDDS(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM, newWidth, newHeight,
            &params, sizeof(ShaderParams))) {
        return false;
    }

    if (newDDS.GetImageCount() < 1) {
        return false;
    }

    const lock_guard<mutex> lock(s_generatedFileTrackerMutex);
    if (getPGD()->isGenerated(newPath)) {
        // already generated
        return true;
    }

    const auto outPath = getPGD()->getGeneratedPath() / newPath;
    filesystem::create_directories(outPath.parent_path());

    DirectX::ScratchImage compressedImage;
    HRESULT hr = DirectX::Compress(newDDS.GetImages(), newDDS.GetImageCount(), newDDS.GetMetadata(),
        DXGI_FORMAT_BC1_UNORM, DirectX::TEX_COMPRESS_DEFAULT, 1.0F, compressedImage);

    if (FAILED(hr)) {
        return false;
    }

    hr = DirectX::SaveToDDSFile(compressedImage.GetImages(), compressedImage.GetImageCount(),
        compressedImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outPath.c_str());

    if (FAILED(hr)) {
        return false;
    }

    // add newly created file to complexMaterialMaps for later processing
    getPGD()->getTextureMap(NIFUtil::TextureSlots::GLOW)[texBase].insert(
        { newPath, NIFUtil::TextureType::SUBSURFACECOLOR });
    getPGD()->setTextureType(newPath, NIFUtil::TextureType::SUBSURFACECOLOR);

    // Update file map
    auto origTexMod = getPGD()->getMod(getDDSPath());
    getPGD()->addGeneratedFile(newPath, origTexMod);

    return true;
}
