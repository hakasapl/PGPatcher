#include "patchers/PatcherMeshPreFixMeshLighting.hpp"

#include "patchers/base/PatcherMeshPre.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>
#include <memory>
#include <utility>

using namespace std;

auto PatcherMeshPreFixMeshLighting::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPre> {
        return make_unique<PatcherMeshPreFixMeshLighting>(nifPath, nif);
    };
}

PatcherMeshPreFixMeshLighting::PatcherMeshPreFixMeshLighting(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath), nif, "FixMeshLighting")
{
}

auto PatcherMeshPreFixMeshLighting::applyPatch([[maybe_unused]] NIFUtil::TextureSet& slots, nifly::NiShape& nifShape)
    -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // not a BSLightingShaderProperty
        return false;
    }

    const auto shaderType = nifShaderBSLSP->GetShaderType();
    if (shaderType == BSLSP_FACE || shaderType == BSLSP_HAIRTINT || shaderType == BSLSP_EYE) {
        // skip facegen
        return false;
    }

    if (nifShaderBSLSP->softlighting <= SOFTLIGHTING_MAX) {
        return false;
    }

    Logger::trace(L"Setting softlighting to 0.6 because it is too high");
    NIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, SOFTLIGHTING_MAX);

    return true;
}
