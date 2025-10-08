#include "patchers/PatcherMeshPostHairFlowMap.hpp"

#include "patchers/base/PatcherMeshPost.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <utility>

using namespace std;

auto PatcherMeshPostHairFlowMap::getFactory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPost> {
        return make_unique<PatcherMeshPostHairFlowMap>(nifPath, nif);
    };
}

PatcherMeshPostHairFlowMap::PatcherMeshPostHairFlowMap(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath), nif, "HairFlowMap")
{
}

auto PatcherMeshPostHairFlowMap::applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // not a BSLightingShaderProperty
        return false;
    }

    if (nifShaderBSLSP->GetShaderType() != BSLightingShaderPropertyShaderType::BSLSP_HAIRTINT) {
        // not a Hair specular shader
        return false;
    }

    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING)) {
        // already has back lighting flag, don't touch it
        return false;
    }

    // Search prefixes
    const auto& normalMap = slots.at(static_cast<int>(NIFUtil::TextureSlots::NORMAL));
    if (normalMap.empty() || !getPGD()->isFile(normalMap)) {
        // no normal map, nothing to do
        return false;
    }

    static const auto flowMapBase = getPGD()->getTextureMapConst(NIFUtil::TextureSlots::BACKLIGHT);

    const auto normalMapBase = NIFUtil::getTexBase(normalMap, NIFUtil::TextureSlots::NORMAL);
    const auto foundMatches = NIFUtil::getTexMatch(normalMapBase, NIFUtil::TextureType::HAIR_FLOWMAP, flowMapBase);
    if (foundMatches.empty()) {
        // no flow map found, nothing to do
        return false;
    }

    // use first match, there shouldn't be more than 1 for this case anyway
    const auto& foundMatch = foundMatches[0];
    // Set the flow map texture slot
    slots[static_cast<int>(NIFUtil::TextureSlots::BACKLIGHT)] = foundMatch.path;

    // Set the back lighting flag
    NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING);

    return true;
}
