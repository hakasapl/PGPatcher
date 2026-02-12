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
    if (s_texToProcess.insert(texPath).second) {
        // only add if not present before
        auto* const pgd = getPGD();
        if (pgd == nullptr) {
            throw runtime_error("PGD is null");
        }
        pgd->addGeneratedFile(getOutputFilename(texPath));
    }
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

    auto* const pgd3d = getPGD3D();
    if (pgd3d == nullptr) {
        throw runtime_error("PGD3D is null");
    }
    return pgd3d->initShader(SHADER_NAME, s_shader);
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

    auto* const pgd = getPGD();
    if (pgd == nullptr) {
        throw runtime_error("PGD is null");
    }
    auto* const pgd3d = getPGD3D();
    if (pgd3d == nullptr) {
        throw runtime_error("PGD3D is null");
    }

    const auto texBase = NIFUtil::getTexBase(getDDSPath(), NIFUtil::TextureSlots::PARALLAX);
    const auto newPath = texBase + L"_m.dds";

    DirectX::ScratchImage newDDS;
    if (!pgd3d->applyShaderToTexture(*getDDS(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM)) {
        return false;
    }

    if (newDDS.GetImageCount() < 1) {
        return false;
    }

    const lock_guard<mutex> lock(s_generatedFileTrackerMutex);

    const auto outPath = pgd->getGeneratedPath() / newPath;
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
    pgd->getTextureMap(NIFUtil::TextureSlots::ENVMASK)[texBase].insert(
        { newPath, NIFUtil::TextureType::COMPLEXMATERIAL });
    pgd->setTextureType(newPath, NIFUtil::TextureType::COMPLEXMATERIAL);

    return true;
}
