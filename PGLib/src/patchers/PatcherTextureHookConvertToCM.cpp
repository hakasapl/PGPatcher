#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include "patchers/base/PatcherTextureHook.hpp"
#include "util/NIFUtil.hpp"

#include <DirectXTex.h>
#include <dxgiformat.h>

#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <winerror.h>
#include <winnt.h>

using namespace std;
using namespace Microsoft::WRL;

auto PatcherTextureHookConvertToCM::addToProcessList(const filesystem::path& texPath) -> void
{
    const unique_lock lock(s_texToProcessMutex);
    s_texToProcess.insert(texPath);
}

auto PatcherTextureHookConvertToCM::isInProcessList(const filesystem::path& texPath) -> bool
{
    const shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

auto PatcherTextureHookConvertToCM::getOutputFilename(const filesystem::path& texPath) -> filesystem::path
{
    const auto texBase = NIFUtil::getTexBase(texPath, NIFUtil::TextureSlots::PARALLAX);
    return texBase + L"_m.dds";
}

auto PatcherTextureHookConvertToCM::initShader() -> bool
{
    if (s_shader != nullptr) {
        return true;
    }

    return getPGD3D()->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookConvertToCM::PatcherTextureHookConvertToCM(std::filesystem::path ddsPath, DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath), dds, "ParallaxToCM")
{
}

auto PatcherTextureHookConvertToCM::applyPatch() -> bool
{
    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = NIFUtil::getTexBase(getDDSPath(), NIFUtil::TextureSlots::PARALLAX);
    const auto newPath = texBase + L"_m.dds";

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

    return true;
}
