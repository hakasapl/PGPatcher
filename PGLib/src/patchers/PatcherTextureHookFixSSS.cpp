#include "patchers/PatcherTextureHookFixSSS.hpp"

#include "PGGlobals.hpp"
#include "PGRunCache.hpp"
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

void PatcherTextureHookFixSSS::reset()
{
    const std::unique_lock lock(s_texToProcessMutex);
    s_texToProcess.clear();
}

void PatcherTextureHookFixSSS::addToProcessList(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::pgd();

    // Record registration for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunCache::recordHookRegistration(PGRunCache::HookKind::FixSSS, texPath);

    // Reuse the output of a previous run if the source texture did not change.
    if (PGRunCache::tryReuseHookOutput(PGRunCache::HookKind::FixSSS, texPath))
        return;

    const std::unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // Only add if not present before.
        pgd->addGeneratedFile(outputFilename(texPath));
    }
}

void PatcherTextureHookFixSSS::replayGenerated(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::pgd();

    const auto texBase = PGNIFUtil::texBase(texPath, PGEnums::TextureSlots::Diffuse);
    const auto newPath = texBase + L"_s.dds";

    pgd->textureMap(PGEnums::TextureSlots::Glow)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::SubsurfaceColor });
    pgd->setTextureType(newPath, PGEnums::TextureType::SubsurfaceColor);
}

bool PatcherTextureHookFixSSS::isInProcessList(const std::filesystem::path& texPath)
{
    const std::shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

std::filesystem::path PatcherTextureHookFixSSS::outputFilename(const std::filesystem::path& texPath)
{
    const auto texBase = PGNIFUtil::texBase(texPath, PGEnums::TextureSlots::Diffuse);
    return texBase + L"_s.dds";
}

bool PatcherTextureHookFixSSS::initShader()
{
    auto* pgd3d = PGGlobals::pgD3D();

    if (s_shader)
        return true;

    return pgd3d->initShader(shaderName, s_shader);
}

PatcherTextureHookFixSSS::PatcherTextureHookFixSSS(std::filesystem::path ddsPath,
                                                   DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath),
                         dds,
                         "SSSFix")
{
}

bool PatcherTextureHookFixSSS::applyPatch()
{
    auto* pgd = PGGlobals::pgd();
    auto* pgd3d = PGGlobals::pgD3D();

    if (!dds())
        throw std::runtime_error("DDS not initialized");

    const auto texBase = PGNIFUtil::texBase(ddsPath(), PGEnums::TextureSlots::Diffuse);
    const auto newPath = texBase + L"_s.dds";

    DirectX::ScratchImage newDDS;
    static constexpr size_t scaleFactor = 2;
    const auto newWidth = static_cast<UINT>(dds()->GetMetadata().width / scaleFactor);
    const auto newHeight = static_cast<UINT>(dds()->GetMetadata().height / scaleFactor);
    // The shader delights and also reduces size by 4 for efficiency.
    ShaderParams params = { .fAlbedoSatPower = shaderAlbedoSatPower, .fAlbedoNorm = shaderAlbedoNorm };
    if (!pgd3d->applyShaderToTexture(
            *dds(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM, newWidth, newHeight, &params, sizeof(ShaderParams))) {
        return false;
    }

    if (newDDS.GetImageCount() < 1)
        return false;

    const std::scoped_lock lock(s_generatedFileTrackerMutex);

    const auto outPath = pgd->generatedPath() / newPath;
    std::filesystem::create_directories(outPath.parent_path());

    DirectX::ScratchImage compressedImage;
    HRESULT hr = DirectX::Compress(newDDS.GetImages(),
                                   newDDS.GetImageCount(),
                                   newDDS.GetMetadata(),
                                   DXGI_FORMAT_BC2_UNORM,
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
    pgd->textureMap(PGEnums::TextureSlots::Glow)[texBase].insert(
        { .path = newPath, .type = PGEnums::TextureType::SubsurfaceColor });
    pgd->setTextureType(newPath, PGEnums::TextureType::SubsurfaceColor);

    // Record generated output for incremental runs.
    PGRunCache::recordHookOutput(PGRunCache::HookKind::FixSSS, ddsPath(), newPath);

    return true;
}
