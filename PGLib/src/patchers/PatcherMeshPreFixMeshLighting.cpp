#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "util/NIFUtil.hpp"

#include "util/Logger.hpp"

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

auto PatcherMeshPreFixMeshLighting::applyPatch(nifly::NiShape& nifShape) -> bool
{
    bool changed = false;

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // not a BSLightingShaderProperty
        return false;
    }

    if (nifShaderBSLSP->softlighting > SOFTLIGHTING_MAX) {
        Logger::trace(L"Setting softlighting to 0.6 because it is too high");
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, SOFTLIGHTING_MAX);
    }

    return changed;
}
