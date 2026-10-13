#include "patchers/PatcherTextureHookFixSSS.hpp"

#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"

#include "SSSFix.h"
#include <DirectXTex.h>
#include <dxgiformat.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <minwindef.h>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>
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
    auto& observer = PGRunObserver::current();
    observer.recordHookRegistration(PGRunObserver::HookKind::FixSSS, texPath);

    // Reuse the output of a previous run if the source texture did not change.
    if (observer.tryReuseHookOutput(PGRunObserver::HookKind::FixSSS, texPath))
        return;

    const std::unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // Only add if not present before.
        pgd->addGeneratedFile(outputFilename(texPath));
    }
}

void PatcherTextureHookFixSSS::replayGenerated(const std::filesystem::path& texPath)
{
    PGGlobals::pgd()->setTextureType(outputFilename(texPath), PGEnums::TextureType::SubsurfaceColor);
}

bool PatcherTextureHookFixSSS::isInProcessList(const std::filesystem::path& texPath)
{
    const std::shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

std::vector<std::filesystem::path> PatcherTextureHookFixSSS::processList()
{
    const std::shared_lock lock(s_texToProcessMutex);
    return { s_texToProcess.begin(), s_texToProcess.end() };
}

std::uint32_t PatcherTextureHookFixSSS::generatorSignature()
{
    static constexpr std::array<std::uint32_t, 6> values {
        static_cast<std::uint32_t>(shaderOutputFormat),     static_cast<std::uint32_t>(outputFormat),
        static_cast<std::uint32_t>(compressFlags),          static_cast<std::uint32_t>(scaleFactor),
        std::bit_cast<std::uint32_t>(shaderAlbedoSatPower), std::bit_cast<std::uint32_t>(shaderAlbedoNorm),
    };
    return signatureOf(PGD3D::ShaderBytecode(g_SSSFix), values);
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

    return pgd3d->initShader(PGD3D::ShaderBytecode(g_SSSFix), s_shader);
}

PatcherTextureHookFixSSS::PatcherTextureHookFixSSS(std::filesystem::path ddsPath,
                                                   DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath),
                         dds,
                         hookName)
{
}

bool PatcherTextureHookFixSSS::generate(DirectX::ScratchImage& compressed)
{
    auto* pgd3d = PGGlobals::pgD3D();

    if (!dds())
        throw std::runtime_error("DDS not initialized");

    DirectX::ScratchImage newDDS;
    const auto newWidth = static_cast<UINT>(dds()->GetMetadata().width / scaleFactor);
    const auto newHeight = static_cast<UINT>(dds()->GetMetadata().height / scaleFactor);
    // The shader delights and also reduces size by 4 for efficiency.
    ShaderParams params = { .fAlbedoSatPower = shaderAlbedoSatPower, .fAlbedoNorm = shaderAlbedoNorm };
    if (!pgd3d->applyShaderToTexture(
            *dds(), newDDS, s_shader, shaderOutputFormat, newWidth, newHeight, &params, sizeof(ShaderParams))) {
        return false;
    }

    if (newDDS.GetImageCount() < 1)
        return false;

    const HRESULT hr = DirectX::Compress(
        newDDS.GetImages(), newDDS.GetImageCount(), newDDS.GetMetadata(), outputFormat, compressFlags, 1, compressed);

    return SUCCEEDED(hr);
}

bool PatcherTextureHookFixSSS::applyPatch()
{
    auto* pgd = PGGlobals::pgd();

    const auto newPath = outputFilename(ddsPath());

    // The shader and the compression run unlocked, so textures are generated in parallel; one file is written and
    // registered at a time.
    DirectX::ScratchImage compressedImage;
    if (!generate(compressedImage))
        return false;

    const std::scoped_lock lock(s_generatedFileTrackerMutex);

    const auto outPath = pgd->generatedPath() / newPath;
    std::filesystem::create_directories(outPath.parent_path());

    const HRESULT hr = DirectX::SaveToDDSFile(compressedImage.GetImages(),
                                              compressedImage.GetImageCount(),
                                              compressedImage.GetMetadata(),
                                              DirectX::DDS_FLAGS_NONE,
                                              outPath.c_str());

    if (FAILED(hr))
        return false;

    // The generated map is deliberately not added to the texture maps: those hold the mapped textures that the shader
    // patchers match directly and that the update cache digests, while a generated map is only reached through this
    // hook. Its type is recorded for the texture type lookups.
    pgd->setTextureType(newPath, PGEnums::TextureType::SubsurfaceColor);

    // Record generated output for incremental runs.
    PGRunObserver::current().recordHookOutput(PGRunObserver::HookKind::FixSSS, ddsPath(), newPath);

    return true;
}
