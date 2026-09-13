#include "patchers/PatcherMeshPostHairFlowMap.hpp"

#include "PGGlobals.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPostHairFlowMap::factory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPost> {
        return std::make_unique<PatcherMeshPostHairFlowMap>(nifPath, nif);
    };
}

PatcherMeshPostHairFlowMap::PatcherMeshPostHairFlowMap(std::filesystem::path nifPath,
                                                       nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath),
                      nif,
                      "HairFlowMap")
{
}

bool PatcherMeshPostHairFlowMap::applyPatch(PGTypes::TextureSet& slots,
                                            nifly::NiShape& nifShape)
{
    auto* pgd = PGGlobals::pgd();

    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // Not a BSLightingShaderProperty.
        return false;
    }

    if (nifShaderBSLSP->GetShaderType() != nifly::BSLightingShaderPropertyShaderType::BSLSP_HAIRTINT) {
        // Not a Hair specular shader.
        return false;
    }

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING)) {
        // Already has back lighting flag, don't touch it.
        return false;
    }

    // Search prefixes.
    const auto& normalMap = slots.at(static_cast<int>(PGEnums::TextureSlots::Normal));
    if (normalMap.empty() || !pgd->isFile(normalMap)) {
        // No normal map, nothing to do.
        return false;
    }

    static const auto flowMapBase = pgd->textureMapConst(PGEnums::TextureSlots::Backlight);

    const auto normalMapBase = PGNIFUtil::texBase(normalMap, PGEnums::TextureSlots::Normal);
    const auto foundMatches = PGNIFUtil::texMatch(normalMapBase, PGEnums::TextureType::HairFlowMap, flowMapBase);
    if (foundMatches.empty()) {
        // No flow map found, nothing to do.
        return false;
    }

    // Use first match, there shouldn't be more than 1 for this case anyway.
    const auto& foundMatch = foundMatches[0];
    // Set the flow map texture slot.
    slots[static_cast<int>(PGEnums::TextureSlots::Backlight)] = foundMatch.path;

    // Set the back lighting flag.
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING);

    return true;
}
