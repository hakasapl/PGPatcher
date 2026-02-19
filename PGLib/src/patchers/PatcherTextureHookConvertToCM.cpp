#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include "PGGlobals.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGNIFUtil.hpp"

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
    auto* pgd = PGGlobals::getPGD();

    const unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // only add if not present before
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
    const auto texBase = PGNIFUtil::getTexBase(texPath, PGEnums::TextureSlots::PARALLAX);
    return texBase + L"_m.dds";
}

auto PatcherTextureHookConvertToCM::initShader() -> bool
{
    auto* pgd3d = PGGlobals::getPGD3D();

    if (s_shader != nullptr) {
        return true;
    }

    return pgd3d->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookConvertToCM::PatcherTextureHookConvertToCM(std::filesystem::path ddsPath,
                                                             DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath),
                         dds,
                         "ParallaxToCM")
{
}

auto PatcherTextureHookConvertToCM::applyPatch() -> bool
{
    auto* pgd = PGGlobals::getPGD();
    auto* pgd3d = PGGlobals::getPGD3D();

    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = PGNIFUtil::getTexBase(getDDSPath(), PGEnums::TextureSlots::PARALLAX);
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
    HRESULT hr = DirectX::Compress(newDDS.GetImages(),
                                   newDDS.GetImageCount(),
                                   newDDS.GetMetadata(),
                                   DXGI_FORMAT_BC3_UNORM,
                                   DirectX::TEX_COMPRESS_DEFAULT,
                                   1.0F,
                                   compressedImage);

    if (FAILED(hr)) {
        return false;
    }

    hr = DirectX::SaveToDDSFile(compressedImage.GetImages(),
                                compressedImage.GetImageCount(),
                                compressedImage.GetMetadata(),
                                DirectX::DDS_FLAGS_NONE,
                                outPath.c_str());

    if (FAILED(hr)) {
        return false;
    }

    // add newly created file to complexMaterialMaps for later processing
    pgd->getTextureMap(PGEnums::TextureSlots::ENVMASK)[texBase].insert(
        {newPath, PGEnums::TextureType::COMPLEXMATERIAL});
    pgd->setTextureType(newPath, PGEnums::TextureType::COMPLEXMATERIAL);

    return true;
}
