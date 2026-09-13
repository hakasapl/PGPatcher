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

void PatcherTextureHookConvertToCM::addToProcessList(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::pgd();

    // Record registration for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordHookRegistration(PGRunCache::HookKind::ConvertToCM, texPath);

    // Reuse the output of a previous run if the source texture did not change.
    if (PGRunCache::tryReuseHookOutput(PGRunCache::HookKind::ConvertToCM, texPath))
        return;

    const std::unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // Only add if not present before.
        pgd->addGeneratedFile(outputFilename(texPath));
    }
}

void PatcherTextureHookConvertToCM::replayGenerated(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::pgd();

    const auto texBase = PGNIFUtil::texBase(texPath, PGEnums::TextureSlots::Parallax);
    const auto newPath = texBase + L"_m.dds";

    pgd->textureMap(PGEnums::TextureSlots::EnvMask)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::ComplexMaterial });
    pgd->setTextureType(newPath, PGEnums::TextureType::ComplexMaterial);
}

bool PatcherTextureHookConvertToCM::isInProcessList(const std::filesystem::path& texPath)
{
    const std::shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

std::filesystem::path PatcherTextureHookConvertToCM::outputFilename(const std::filesystem::path& texPath)
{
    const auto texBase = PGNIFUtil::texBase(texPath, PGEnums::TextureSlots::Parallax);
    return texBase + L"_m.dds";
}

bool PatcherTextureHookConvertToCM::initShader()
{
    auto* pgd3d = PGGlobals::pGD3D();

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

bool PatcherTextureHookConvertToCM::applyPatch()
{
    auto* pgd = PGGlobals::pgd();
    auto* pgd3d = PGGlobals::pGD3D();

    if (dds() == nullptr)
        throw std::runtime_error("DDS not initialized");

    const auto texBase = PGNIFUtil::texBase(ddsPath(), PGEnums::TextureSlots::Parallax);
    const auto newPath = texBase + L"_m.dds";

    DirectX::ScratchImage newDDS;
    if (!pgd3d->applyShaderToTexture(*dds(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM))
        return false;

    if (newDDS.GetImageCount() < 1)
        return false;

    const std::scoped_lock lock(s_generatedFileTrackerMutex);

    const auto outPath = pgd->generatedPath() / newPath;
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
    pgd->textureMap(PGEnums::TextureSlots::EnvMask)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::ComplexMaterial });
    pgd->setTextureType(newPath, PGEnums::TextureType::ComplexMaterial);

    // Record generated output for incremental runs.
    PGRunCache::recordHookOutput(PGRunCache::HookKind::ConvertToCM, ddsPath(), newPath);

    return true;
}
