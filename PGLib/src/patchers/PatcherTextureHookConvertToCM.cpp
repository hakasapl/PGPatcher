#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include <DirectXTex.h>
#include <mutex>

using namespace std;
using namespace Microsoft::WRL;

auto PatcherTextureHookConvertToCM::initShader() -> bool
{
    if (s_shader != nullptr) {
        return true;
    }

    return getPGD3D()->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookConvertToCM::PatcherTextureHookConvertToCM(filesystem::path texPath)
    : PatcherTextureHook(std::move(texPath), "ParallaxToCM")
{
}

auto PatcherTextureHookConvertToCM::applyPatch(filesystem::path& newPath) -> bool
{
    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = NIFUtil::getTexBase(getDDSPath(), NIFUtil::TextureSlots::PARALLAX);
    newPath = texBase + L"_m.dds";

    if (getPGD()->isGenerated(newPath)) {
        // already generated
        return true;
    }

    DirectX::ScratchImage newDDS;
    if (!getPGD3D()->applyShaderToTexture(*getDDS(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM)) {
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
        DXGI_FORMAT_BC3_UNORM, DirectX::TEX_COMPRESS_DEFAULT, 1.0F, compressedImage);

    if (FAILED(hr)) {
        return false;
    }

    hr = DirectX::SaveToDDSFile(compressedImage.GetImages(), compressedImage.GetImageCount(),
        compressedImage.GetMetadata(), DirectX::DDS_FLAGS_NONE, outPath.c_str());

    if (FAILED(hr)) {
        return false;
    }

    // add newly created file to complexMaterialMaps for later processing
    getPGD()->getTextureMap(NIFUtil::TextureSlots::ENVMASK)[texBase].insert(
        { newPath, NIFUtil::TextureType::COMPLEXMATERIAL });
    getPGD()->setTextureType(newPath, NIFUtil::TextureType::COMPLEXMATERIAL);

    // Update file map
    auto origTexMod = getPGD()->getMod(getDDSPath());
    getPGD()->addGeneratedFile(newPath, origTexMod);

    return true;
}
