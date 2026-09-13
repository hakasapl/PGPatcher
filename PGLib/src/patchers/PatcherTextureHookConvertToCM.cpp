#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include "PGGlobals.hpp"
#include "PGRunCache.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGEnums.hpp"
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

using namespace Microsoft::WRL;

void PatcherTextureHookConvertToCM::reset()
{
    const std::unique_lock lock(s_texToProcessMutex);
    s_texToProcess.clear();
}

auto PatcherTextureHookConvertToCM::addToProcessList(const std::filesystem::path& texPath) -> void
{
    auto* pgd = PGGlobals::getPGD();

    // Record registration for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordHookRegistration(PGRunCache::HookKind::ConvertToCM, texPath);

    // Reuse the output of a previous run if the source texture did not change.
    if (PGRunCache::tryReuseHookOutput(PGRunCache::HookKind::ConvertToCM, texPath))
        return;

    const std::unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // Only add if not present before.
        pgd->addGeneratedFile(getOutputFilename(texPath));
    }
}

void PatcherTextureHookConvertToCM::replayGenerated(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::getPGD();

    const auto texBase = PGNIFUtil::getTexBase(texPath, PGEnums::TextureSlots::Parallax);
    const auto newPath = texBase + L"_m.dds";

    pgd->getTextureMap(PGEnums::TextureSlots::EnvMask)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::ComplexMaterial });
    pgd->setTextureType(newPath, PGEnums::TextureType::ComplexMaterial);
}

auto PatcherTextureHookConvertToCM::isInProcessList(const std::filesystem::path& texPath) -> bool
{
    const std::shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

auto PatcherTextureHookConvertToCM::getOutputFilename(const std::filesystem::path& texPath) -> std::filesystem::path
{
    const auto texBase = PGNIFUtil::getTexBase(texPath, PGEnums::TextureSlots::Parallax);
    return texBase + L"_m.dds";
}

auto PatcherTextureHookConvertToCM::initShader() -> bool
{
    auto* pgd3d = PGGlobals::getPGD3D();

    if (s_shader != nullptr)
        return true;

    return pgd3d->initShader(shaderName, s_shader);
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

    if (getDDS() == nullptr)
        throw std::runtime_error("DDS not initialized");

    const auto texBase = PGNIFUtil::getTexBase(getDDSPath(), PGEnums::TextureSlots::Parallax);
    const auto newPath = texBase + L"_m.dds";

    DirectX::ScratchImage newDDS;
    if (!pgd3d->applyShaderToTexture(*getDDS(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM))
        return false;

    if (newDDS.GetImageCount() < 1)
        return false;

    const std::scoped_lock lock(s_generatedFileTrackerMutex);

    const auto outPath = pgd->getGeneratedPath() / newPath;
    std::filesystem::create_directories(outPath.parent_path());

    DirectX::ScratchImage compressedImage;
    HRESULT hr = DirectX::Compress(newDDS.GetImages(),
                                   newDDS.GetImageCount(),
                                   newDDS.GetMetadata(),
                                   DXGI_FORMAT_BC3_UNORM,
                                   DirectX::TEX_COMPRESS_DEFAULT,
                                   1,
                                   compressedImage);

    if (FAILED(hr))
        return false;

    hr = DirectX::SaveToDDSFile(compressedImage.GetImages(),
                                compressedImage.GetImageCount(),
                                compressedImage.GetMetadata(),
                                DirectX::DDS_FLAGS_NONE,
                                outPath.c_str());

    if (FAILED(hr))
        return false;

    // Add newly created file to complexMaterialMaps for later processing.
    pgd->getTextureMap(PGEnums::TextureSlots::EnvMask)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::ComplexMaterial });
    pgd->setTextureType(newPath, PGEnums::TextureType::ComplexMaterial);

    // Record generated output for incremental runs.
    PGRunCache::recordHookOutput(PGRunCache::HookKind::ConvertToCM, getDDSPath(), newPath);

    return true;
}
