#include "patchers/PatcherTextureHookConvertToCM.hpp"

#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "patchers/base/PatcherTextureHook.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"

#include "ParallaxToCM.h"
#include <DirectXTex.h>
#include <dxgiformat.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <utility>
#include <vector>
#include <winerror.h>
#include <winnt.h>

void PatcherTextureHookConvertToCM::reset()
{
    const std::unique_lock lock(s_texToProcessMutex);
    s_texToProcess.clear();
}

void PatcherTextureHookConvertToCM::addToProcessList(const std::filesystem::path& texPath)
{
    auto* pgd = PGGlobals::pgd();

    // Record registration for incremental runs (no-op unless a mesh is being recorded on this thread).
    auto& observer = PGRunObserver::current();
    observer.recordHookRegistration(PGRunObserver::HookKind::ConvertToCM, texPath);

    // Reuse the output of a previous run if the source texture did not change.
    if (observer.tryReuseHookOutput(PGRunObserver::HookKind::ConvertToCM, texPath))
        return;

    const std::unique_lock lock(s_texToProcessMutex);
    if (s_texToProcess.insert(texPath).second) {
        // Only add if not present before.
        pgd->addGeneratedFile(outputFilename(texPath));
    }
}

void PatcherTextureHookConvertToCM::replayGenerated(const std::filesystem::path& texPath)
{
    PGGlobals::pgd()->setTextureType(outputFilename(texPath), PGEnums::TextureType::ComplexMaterial);
}

bool PatcherTextureHookConvertToCM::isInProcessList(const std::filesystem::path& texPath)
{
    const std::shared_lock lock(s_texToProcessMutex);
    return s_texToProcess.contains(texPath);
}

std::vector<std::filesystem::path> PatcherTextureHookConvertToCM::processList()
{
    const std::shared_lock lock(s_texToProcessMutex);
    return { s_texToProcess.begin(), s_texToProcess.end() };
}

std::uint32_t PatcherTextureHookConvertToCM::generatorSignature()
{
    static constexpr std::array<std::uint32_t, 3> values {
        static_cast<std::uint32_t>(shaderOutputFormat),
        static_cast<std::uint32_t>(outputFormat),
        static_cast<std::uint32_t>(compressFlags),
    };
    return signatureOf(PGD3D::ShaderBytecode(g_ParallaxToCM), values);
}

std::filesystem::path PatcherTextureHookConvertToCM::outputFilename(const std::filesystem::path& texPath)
{
    const auto texBase = PGNIFUtil::texBase(texPath, PGEnums::TextureSlots::Parallax);
    return texBase + L"_m.dds";
}

bool PatcherTextureHookConvertToCM::initShader()
{
    auto* pgd3d = PGGlobals::pgD3D();

    if (s_shader)
        return true;

    return pgd3d->initShader(PGD3D::ShaderBytecode(g_ParallaxToCM), s_shader);
}

PatcherTextureHookConvertToCM::PatcherTextureHookConvertToCM(std::filesystem::path ddsPath,
                                                             DirectX::ScratchImage* dds)
    : PatcherTextureHook(std::move(ddsPath),
                         dds,
                         hookName)
{
}

bool PatcherTextureHookConvertToCM::generate(DirectX::ScratchImage& compressed)
{
    auto* pgd3d = PGGlobals::pgD3D();

    if (!dds())
        throw std::runtime_error("DDS not initialized");

    DirectX::ScratchImage newDDS;
    if (!pgd3d->applyShaderToTexture(*dds(), newDDS, s_shader, shaderOutputFormat))
        return false;

    if (newDDS.GetImageCount() < 1)
        return false;

    const HRESULT hr = DirectX::Compress(
        newDDS.GetImages(), newDDS.GetImageCount(), newDDS.GetMetadata(), outputFormat, compressFlags, 1, compressed);

    return SUCCEEDED(hr);
}

bool PatcherTextureHookConvertToCM::applyPatch()
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
    pgd->setTextureType(newPath, PGEnums::TextureType::ComplexMaterial);

    // Record generated output for incremental runs.
    PGRunObserver::current().recordHookOutput(PGRunObserver::HookKind::ConvertToCM, ddsPath(), newPath);

    return true;
}
