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

using namespace std;

auto PatcherMeshShaderVanillaParallax::getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshShader> {
        return make_unique<PatcherMeshShaderVanillaParallax>(nifPath, nif);
    };
}

auto PatcherMeshShaderVanillaParallax::getShaderType() -> PGEnums::ShapeShader
{
    return PGEnums::ShapeShader::VANILLAPARALLAX;
}

PatcherMeshShaderVanillaParallax::PatcherMeshShaderVanillaParallax(filesystem::path nifPath,
                                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "VanillaParallax")
{
    if (nif != nullptr) {
        // Determine if NIF has attached havok animations
        vector<NiObject*> nifBlockTree;
        nif->GetTree(nifBlockTree);

        for (NiObject* nifBlock : nifBlockTree) {
            if (boost::iequals(nifBlock->GetBlockName(), "BSBehaviorGraphExtraData")) {
                m_hasAttachedHavok = true;
            }
        }
    }
}

auto PatcherMeshShaderVanillaParallax::canApply(NiShape& nifShape,
                                                bool singlepassMATO,
                                                const PGPlugin::ModelRecordType& modelRecordType) -> bool
{
    if (modelRecordType == PGPlugin::ModelRecordType::GRASS) {
        // grass is not supported
        return false;
    }

    if (singlepassMATO) {
        return false;
    }

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Check if nif has attached havok (Results in crashes for vanilla Parallax)
    if (m_hasAttachedHavok) {
        return false;
    }

    // ignore skinned meshes, these don't support Parallax
    if (nifShape.HasSkinInstance() || nifShape.IsSkinned()) {
        return false;
    }

    if (nifShape.HasAlphaProperty()) {
        return false;
    }

    // Check for shader type
    auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != BSLSP_DEFAULT && nifShaderType != BSLSP_PARALLAX && nifShaderType != BSLSP_ENVMAP) {
        // don't overwrite existing NIFShaders
        return false;
    }

    // decals don't work with regular Parallax
    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF1_DECAL)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF1_DYNAMIC_DECAL)) {
        return false;
    }

    // Mesh lighting doesn't work with regular Parallax
    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
        || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING)) {
        return false;
    }

    return true;
}

auto PatcherMeshShaderVanillaParallax::shouldApply(nifly::NiShape& nifShape,
                                                   std::vector<PatcherMatch>& matches) -> bool
{
    return shouldApply(getTextureSet(getNIFPath(), *getNIF(), nifShape), matches);
}

auto PatcherMeshShaderVanillaParallax::shouldApply(const PGTypes::TextureSet& oldSlots,
                                                   std::vector<PatcherMatch>& matches) -> bool
{
    auto* pgd = PGGlobals::getPGD();
    auto* pgd3d = PGGlobals::getPGD3D();

    static const auto heightBaseMap = pgd->getTextureMapConst(PGEnums::TextureSlots::PARALLAX);

    matches.clear();

    // Search prefixes
    const auto searchPrefixes = PGNIFUtil::getSearchPrefixes(oldSlots);

    // Check if parallax file exists
    static const vector<int> slotSearch = {1, 0}; // Diffuse first, then normal
    filesystem::path baseMap;
    vector<PGTypes::PGTexture> foundMatches;
    PGEnums::TextureSlots matchedFromSlot = PGEnums::TextureSlots::NORMAL;
    for (const int& slot : slotSearch) {
        baseMap = oldSlots.at(slot);
        if (baseMap.empty() || !pgd->isFile(baseMap)) {
            continue;
        }

        foundMatches.clear();
        foundMatches = PGNIFUtil::getTexMatch(searchPrefixes.at(slot), PGEnums::TextureType::HEIGHT, heightBaseMap);

        if (!foundMatches.empty()) {
            // TODO should we be trying diffuse after normal too and present all options?
            matchedFromSlot = static_cast<PGEnums::TextureSlots>(slot);
            break;
        }
    }

    // Check aspect ratio matches
    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (pgd3d->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;
            curMatch.matchedFrom.insert(matchedFromSlot);
            if (match.path == oldSlots[static_cast<size_t>(PGEnums::TextureSlots::PARALLAX)]) {
                lastMatch = curMatch; // Save the match that equals OldSlots[Slot]
            } else {
                matches.push_back(curMatch); // Add other matches
            }
        }
    }

    if (!lastMatch.matchedPath.empty()) {
        matches.push_back(lastMatch); // Add the match that equals OldSlots[Slot]
    }

    return !matches.empty();
}

void PatcherMeshShaderVanillaParallax::applyPatch(PGTypes::TextureSet& slots,
                                                  nifly::NiShape& nifShape,
                                                  const PatcherMatch& match)
{
    // Apply shader
    applyShader(nifShape);

    // Apply slots
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderVanillaParallax::applyPatchSlots(PGTypes::TextureSet& slots,
                                                       const PatcherMatch& match)
{
    slots[static_cast<size_t>(PGEnums::TextureSlots::PARALLAX)] = match.matchedPath;
}

void PatcherMeshShaderVanillaParallax::applyShader(nifly::NiShape& nifShape)
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to Parallax
    PGNIFUtil::setShaderType(nifShader, BSLSP_PARALLAX);
    // Set NIFShader flags
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    // Set vertex colors for shape
    if (!nifShape.HasVertexColors()) {
        nifShape.SetVertexColors(true);
    }
    // Set vertex colors for NIFShader
    if (!nifShader->HasVertexColors()) {
        nifShader->SetVertexColors(true);
    }
}
