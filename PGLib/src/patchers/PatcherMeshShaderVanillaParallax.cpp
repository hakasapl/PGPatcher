#include "patchers/PatcherMeshShaderVanillaParallax.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

auto PatcherMeshShaderVanillaParallax::factory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshShader> {
        return std::make_unique<PatcherMeshShaderVanillaParallax>(nifPath, nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderVanillaParallax::shaderType() { return PGEnums::ShapeShader::VanillaParallax; }

PatcherMeshShaderVanillaParallax::PatcherMeshShaderVanillaParallax(std::filesystem::path nifPath,
                                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "VanillaParallax")
{
    if (nif) {
        // Determine if NIF has attached havok animations.
        std::vector<nifly::NiObject*> nifBlockTree;
        nif->GetTree(nifBlockTree);

        for (nifly::NiObject* nifBlock : nifBlockTree)
            if (boost::iequals(nifBlock->GetBlockName(), "BSBehaviorGraphExtraData"))
                m_hasAttachedHavok = true;
    }
}

bool PatcherMeshShaderVanillaParallax::canApply(nifly::NiShape& nifShape,
                                                bool isSinglepassMATO,
                                                const PGPlugin::ModelRecordType& modelRecordType)
{
    if (modelRecordType == PGPlugin::ModelRecordType::Grass) {
        // Grass is not supported.
        return false;
    }

    if (isSinglepassMATO)
        return false;

    auto* nifShader = nif()->GetShader(&nifShape);
    const auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Check if nif has attached havok (Results in crashes for vanilla Parallax).
    if (m_hasAttachedHavok)
        return false;

    // Ignore skinned meshes, these don't support Parallax.
    if (nifShape.HasSkinInstance() || nifShape.IsSkinned())
        return false;

    if (nifShape.HasAlphaProperty())
        return false;

    // Check for shader type.
    const auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != nifly::BSLSP_DEFAULT && nifShaderType != nifly::BSLSP_PARALLAX
        && nifShaderType != nifly::BSLSP_ENVMAP) {
        // Don't overwrite existing NIFShaders.
        return false;
    }

    // Decals don't work with regular Parallax.
    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF1_DECAL)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF1_DYNAMIC_DECAL)) {
        return false;
    }

    // Mesh lighting doesn't work with regular Parallax.
    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING)) {
        return false;
    }

    // Anisotropic lighting doesn't work with regular Parallax.
    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_ANISOTROPIC_LIGHTING))
        return false;

    return true;
}

bool PatcherMeshShaderVanillaParallax::shouldApply(nifly::NiShape& nifShape,
                                                   std::vector<PatcherMatch>& matches)
{
    return shouldApply(textureSet(nifPath(), *nif(), nifShape), matches);
}

bool PatcherMeshShaderVanillaParallax::shouldApply(const PGTypes::TextureSet& oldSlots,
                                                   std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();
    auto* pgd3d = PGGlobals::pgD3D();

    static const auto heightBaseMap = pgd->textureMapConst(PGEnums::TextureSlots::Parallax);

    matches.clear();

    // Search prefixes.
    const auto searchPrefixes = PGNIFUtil::searchPrefixes(oldSlots);

    // Check if parallax file exists.
    static const std::vector<int> slotSearch = { 1, 0 }; // Diffuse first, then normal
    std::filesystem::path baseMap;
    std::vector<PGTypes::PGTexture> foundMatches;
    for (const int& slot : slotSearch) {
        baseMap = oldSlots.at(slot);
        if (baseMap.empty() || !pgd->isFile(baseMap))
            continue;

        foundMatches.clear();
        foundMatches = PGNIFUtil::texMatch(searchPrefixes.at(slot), PGEnums::TextureType::Height, heightBaseMap);

        if (!foundMatches.empty()) {
            // FIXME: Consider trying diffuse after normal too and presenting all options.
            break;
        }
    }

    // Check aspect ratio matches.
    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (pgd3d->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;
            if (match.path == oldSlots[static_cast<size_t>(PGEnums::TextureSlots::Parallax)])
                lastMatch = curMatch; // Save the match that equals OldSlots[Slot]
            else
                matches.push_back(curMatch); // Add other matches
        }
    }

    if (!lastMatch.matchedPath.empty())
        matches.push_back(lastMatch); // Add the match that equals OldSlots[Slot]

    return !matches.empty();
}

void PatcherMeshShaderVanillaParallax::applyPatch(PGTypes::TextureSet& slots,
                                                  nifly::NiShape& nifShape,
                                                  const PatcherMatch& match)
{
    // Apply shader.
    applyShader(nifShape);

    // Apply slots.
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderVanillaParallax::applyPatchSlots(PGTypes::TextureSet& slots,
                                                       const PatcherMatch& match)
{
    slots[static_cast<size_t>(PGEnums::TextureSlots::Parallax)] = match.matchedPath;
}

void PatcherMeshShaderVanillaParallax::applyShader(nifly::NiShape& nifShape)
{
    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to Parallax.
    PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_PARALLAX);
    // Set NIFShader flags.
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    // Set vertex colors for shape.
    if (!nifShape.HasVertexColors())
        nifShape.SetVertexColors(true);
    // Set vertex colors for NIFShader.
    if (!nifShader->HasVertexColors())
        nifShader->SetVertexColors(true);
}
