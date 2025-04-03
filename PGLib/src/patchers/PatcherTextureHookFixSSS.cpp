#include "patchers/PatcherTextureHookFixSSS.hpp"
#include <DirectXTex.h>
#include <dxgiformat.h>

using namespace std;
using namespace Microsoft::WRL;

auto PatcherTextureHookFixSSS::initShader() -> bool
{
    if (s_shader != nullptr) {
        return true;
    }

    return getPGD3D()->initShader(SHADER_NAME, s_shader);
}

PatcherTextureHookFixSSS::PatcherTextureHookFixSSS(filesystem::path texPath)
    : PatcherTextureHook(std::move(texPath), "SSSFix")
{
}

auto PatcherTextureHookFixSSS::applyPatch(filesystem::path& newPath) -> bool
{
    if (getDDS() == nullptr) {
        throw runtime_error("DDS not initialized");
    }

    const auto texBase = NIFUtil::getTexBase(getDDSPath(), NIFUtil::TextureSlots::DIFFUSE);
    newPath = texBase + L"_s.dds";

    if (getPGD()->isGenerated(newPath)) {
        // already generated
        return true;
    }

    DirectX::ScratchImage newDDS;
    ShaderParams params = { .fAlbedoSatPower = SHADER_ALBEDO_SAT_POWER, .fAlbedoNorm = SHADER_ALBEDO_NORM };
    if (!getPGD3D()->applyShaderToTexture(
            *getDDS(), newDDS, s_shader, DXGI_FORMAT_R8G8B8A8_UNORM, &params, sizeof(ShaderParams))) {
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

    const HRESULT hr = DirectX::SaveToDDSFile(
        newDDS.GetImages(), newDDS.GetImageCount(), newDDS.GetMetadata(), DirectX::DDS_FLAGS_NONE, outPath.c_str());

    if (FAILED(hr)) {
        return false;
    }

    // add newly created file to complexMaterialMaps for later processing
    getPGD()->getTextureMap(NIFUtil::TextureSlots::GLOW)[texBase].insert(
        { newPath, NIFUtil::TextureType::SUBSURFACECOLOR });
    getPGD()->setTextureType(newPath, NIFUtil::TextureType::SUBSURFACECOLOR);

    // Update file map
    auto origTexMod = getPGD()->getMod(getDDSPath());
    getPGD()->addGeneratedFile(newPath, origTexMod);

    return true;
}
