#include "patchers/PatcherMeshShaderVanillaParallax.hpp"

#include "patchers/base/PatcherMeshShader.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"

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

auto PatcherMeshShaderVanillaParallax::getShaderType() -> NIFUtil::ShapeShader
{
    return NIFUtil::ShapeShader::VANILLAPARALLAX;
}

PatcherMeshShaderVanillaParallax::PatcherMeshShaderVanillaParallax(filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath), nif, "VanillaParallax")
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

auto PatcherMeshShaderVanillaParallax::canApply(NiShape& nifShape, bool singlepassMATO) -> bool
{
    if (singlepassMATO) {
        Logger::trace(L"Cannot Apply: Singlepass MATO enabled");
        return false;
    }

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Check if nif has attached havok (Results in crashes for vanilla Parallax)
    if (m_hasAttachedHavok) {
        Logger::trace(L"Cannot Apply: Attached havok animations");
        return false;
    }

    // ignore skinned meshes, these don't support Parallax
    if (nifShape.HasSkinInstance() || nifShape.IsSkinned()) {
        Logger::trace(L"Cannot Apply: Skinned mesh");
        return false;
    }

    if (nifShape.HasAlphaProperty()) {
        Logger::trace(L"Cannot Apply: Shape has alpha property");
        return false;
    }

    // Check for shader type
    auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != BSLSP_DEFAULT && nifShaderType != BSLSP_PARALLAX && nifShaderType != BSLSP_ENVMAP) {
        // don't overwrite existing NIFShaders
        Logger::trace(L"Cannot Apply: Incorrect NIFShader type");
        return false;
    }

    // decals don't work with regular Parallax
    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF1_DECAL)
        || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF1_DYNAMIC_DECAL)) {
        Logger::trace(L"Cannot Apply: Shape has decal");
        return false;
    }

    // Mesh lighting doesn't work with regular Parallax
    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
        || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
        || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING)) {
        Logger::trace(L"Cannot Apply: Lighting on shape");
        return false;
    }

    return true;
}

auto PatcherMeshShaderVanillaParallax::shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool
{
    return shouldApply(getTextureSet(getNIFPath(), *getNIF(), nifShape), matches);
}

auto PatcherMeshShaderVanillaParallax::shouldApply(
    const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches) -> bool
{
    static const auto heightBaseMap = getPGD()->getTextureMapConst(NIFUtil::TextureSlots::PARALLAX);

    matches.clear();

    // Search prefixes
    const auto searchPrefixes = NIFUtil::getSearchPrefixes(oldSlots);

    // Check if parallax file exists
    static const vector<int> slotSearch = { 1, 0 }; // Diffuse first, then normal
    filesystem::path baseMap;
    vector<NIFUtil::PGTexture> foundMatches;
    NIFUtil::TextureSlots matchedFromSlot = NIFUtil::TextureSlots::NORMAL;
    for (const int& slot : slotSearch) {
        baseMap = oldSlots.at(slot);
        if (baseMap.empty() || !getPGD()->isFile(baseMap)) {
            continue;
        }

        foundMatches.clear();
        foundMatches = NIFUtil::getTexMatch(searchPrefixes.at(slot), NIFUtil::TextureType::HEIGHT, heightBaseMap);

        if (!foundMatches.empty()) {
            // TODO should we be trying diffuse after normal too and present all options?
            matchedFromSlot = static_cast<NIFUtil::TextureSlots>(slot);
            break;
        }
    }

    // Check aspect ratio matches
    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (getPGD3D()->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;
            curMatch.matchedFrom.insert(matchedFromSlot);
            if (match.path == oldSlots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)]) {
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

void PatcherMeshShaderVanillaParallax::applyPatch(
    NIFUtil::TextureSet& slots, nifly::NiShape& nifShape, const PatcherMatch& match)
{
    // Apply shader
    applyShader(nifShape);

    // Apply slots
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderVanillaParallax::applyPatchSlots(NIFUtil::TextureSet& slots, const PatcherMatch& match)
{
    slots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)] = match.matchedPath;
}

void PatcherMeshShaderVanillaParallax::applyShader(nifly::NiShape& nifShape)
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to Parallax
    NIFUtil::setShaderType(nifShader, BSLSP_PARALLAX);
    // Set NIFShader flags
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);
    NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    // Set vertex colors for shape
    if (!nifShape.HasVertexColors()) {
        nifShape.SetVertexColors(true);
    }
    // Set vertex colors for NIFShader
    if (!nifShader->HasVertexColors()) {
        nifShader->SetVertexColors(true);
    }
}
