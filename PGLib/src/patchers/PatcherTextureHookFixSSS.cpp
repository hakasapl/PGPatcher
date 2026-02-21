#include "patchers/PatcherTextureHookFixSSS.hpp"

#include "PGGlobals.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"

#include <DirectXTex.h>
#include <dxgiformat.h>

#include <cstddef>
#include <filesystem>
#include <minwindef.h>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <winerror.h>
#include <winnt.h>

using namespace std;
using namespace Microsoft::WRL;

auto PatcherTextureHookFixSSS::addToProcessList(const filesystem::path& texPath) -> void
{
    auto* pgd = PGGlobals::getPGD();

    const unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // only add if not present before
        pgd->addGeneratedFile(getOutputFilename(texPath));
    }
}

auto PatcherTextureHookFixSSS::isInProcessList(const filesystem::path& texPath) -> bool
{
    const shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

auto PatcherTextureHookFixSSS::getOutputFilename(const filesystem::path& texPath) -> filesystem::path
{
    const auto texBase = PGNIFUtil::getTexBase(texPath, PGEnums::TextureSlots::DIFFUSE);
    return texBase + L"_s.dds";
}

auto PatcherTextureHookFixSSS::initShader() -> bool
{
    auto* pgd3d = PGGlobals::getPGD3D();

    if (s_shader != nullptr) {
        return true;
    }

    return pgd3d->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookFixSSS::PatcherTextureHookFixSSS(std::filesystem::path ddsPath,
                                                   DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath),
                         dds,
                         "SSSFix")
{
}

auto PatcherTextureHookFixSSS::applyPatch() -> bool
{
    auto* pgd = PGGlobals::getPGD();
    auto* pgd3d = PGGlobals::getPGD3D();

    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = PGNIFUtil::getTexBase(getDDSPath(), PGEnums::TextureSlots::DIFFUSE);
    const auto newPath = texBase + L"_s.dds";

    DirectX::ScratchImage newDDS;
    static constexpr size_t SCALE_FACTOR = 2;
    const auto newWidth = static_cast<UINT>(getDDS()->GetMetadata().width / SCALE_FACTOR);
    const auto newHeight = static_cast<UINT>(getDDS()->GetMetadata().height / SCALE_FACTOR);
    // the shader delights and also reduces size by 4 for efficiency
    ShaderParams params = {.fAlbedoSatPower = SHADER_ALBEDO_SAT_POWER, .fAlbedoNorm = SHADER_ALBEDO_NORM};
    if (!pgd3d->applyShaderToTexture(*getDDS(),
                                     newDDS,
                                     s_shader,
                                     DXGI_FORMAT_R8G8B8A8_UNORM,
                                     newWidth,
                                     newHeight,
                                     &params,
                                     sizeof(ShaderParams))) {
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
                                   DXGI_FORMAT_BC2_UNORM,
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
    pgd->getTextureMap(PGEnums::TextureSlots::GLOW)[texBase].insert({newPath, PGEnums::TextureType::SUBSURFACECOLOR});
    pgd->setTextureType(newPath, PGEnums::TextureType::SUBSURFACECOLOR);

    return true;
}
