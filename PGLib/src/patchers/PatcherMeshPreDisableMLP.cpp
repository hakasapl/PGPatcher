#include "patchers/PatcherMeshPreDisableMLP.hpp"

#include "patchers/base/PatcherMeshPre.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>
#include <memory>
#include <utility>

using namespace std;

auto PatcherMeshPreDisableMLP::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPre> {
        return make_unique<PatcherMeshPreDisableMLP>(nifPath, nif);
    };
}

PatcherMeshPreDisableMLP::PatcherMeshPreDisableMLP(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath), nif, "FixMeshLighting")
{
}

auto PatcherMeshPreDisableMLP::applyPatch([[maybe_unused]] NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool
{
    bool patched = false;

    // get shader
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        return false; // No shader, nothing to do
    }

    if (nifShaderBSLSP->GetShaderType() != BSLSP_MULTILAYERPARALLAX) {
        return false; // Not MLP, nothing to do
    }

    // set shader to default
    patched = true;
    nifShaderBSLSP->SetShaderType(BSLSP_DEFAULT);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);

    slots[static_cast<int>(NIFUtil::TextureSlots::GLOW)] = L"";
    slots[static_cast<int>(NIFUtil::TextureSlots::MULTILAYER)] = L"";
    slots[static_cast<int>(NIFUtil::TextureSlots::CUBEMAP)] = L"";
    slots[static_cast<int>(NIFUtil::TextureSlots::ENVMASK)] = L"";
    slots[static_cast<int>(NIFUtil::TextureSlots::BACKLIGHT)] = L"";

    return patched;
}
