#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include <Shaders.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>

#include "Logger.hpp"
#include "NIFUtil.hpp"

using namespace std;

// Statics
std::vector<wstring> PatcherMeshShaderComplexMaterial::s_dynCubemapBlocklist;
bool PatcherMeshShaderComplexMaterial::s_disableMLP;

auto PatcherMeshShaderComplexMaterial::loadStatics(
    const bool& disableMLP, const std::vector<std::wstring>& dynCubemapBlocklist) -> void
{
    PatcherMeshShaderComplexMaterial::s_dynCubemapBlocklist = dynCubemapBlocklist;
    PatcherMeshShaderComplexMaterial::s_disableMLP = disableMLP;
}

auto PatcherMeshShaderComplexMaterial::getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshShader> {
        return make_unique<PatcherMeshShaderComplexMaterial>(nifPath, nif);
    };
}

auto PatcherMeshShaderComplexMaterial::getShaderType() -> NIFUtil::ShapeShader
{
    return NIFUtil::ShapeShader::COMPLEXMATERIAL;
}

PatcherMeshShaderComplexMaterial::PatcherMeshShaderComplexMaterial(filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath), nif, "ComplexMaterial")
{
}

auto PatcherMeshShaderComplexMaterial::canApply(NiShape& nifShape) -> bool
{
    // Prep
    Logger::trace(L"Starting checking");

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Get NIFShader type
    auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != BSLSP_DEFAULT && nifShaderType != BSLSP_ENVMAP && nifShaderType != BSLSP_PARALLAX
        && (nifShaderType != BSLSP_MULTILAYERPARALLAX || !s_disableMLP)) {
        Logger::trace(L"Shape Rejected: Incorrect NIFShader type");
        return false;
    }

    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_ANISOTROPIC_LIGHTING)
        && (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
            || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
            || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING))) {
        Logger::trace(L"Shape Rejected: Unable shader permutation");
        return false;
    }

    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
        && NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
        && NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING)) {
        Logger::trace(L"Shape Rejected: Unable shader permutation");
        return false;
    }

    Logger::trace(L"Shape Accepted");
    return true;
}

auto PatcherMeshShaderComplexMaterial::shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool
{
    // Check for CM matches
    return shouldApply(getTextureSet(nifShape), matches);
}

auto PatcherMeshShaderComplexMaterial::shouldApply(
    const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches) -> bool
{
    static const auto cmBaseMap = getPGD()->getTextureMapConst(NIFUtil::TextureSlots::ENVMASK);

    matches.clear();

    // Search prefixes
    const auto searchPrefixes = NIFUtil::getSearchPrefixes(oldSlots);

    // Check if complex material file exists
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
        foundMatches = NIFUtil::getTexMatch(searchPrefixes.at(slot), NIFUtil::TextureType::COMPLEXMATERIAL, cmBaseMap);

        if (!foundMatches.empty()) {
            // TODO should we be trying diffuse after normal too and present all options?
            matchedFromSlot = static_cast<NIFUtil::TextureSlots>(slot);
            break;
        }
    }

    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (getPGD3D()->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;
            curMatch.matchedFrom.insert(matchedFromSlot);
            if (match.path == oldSlots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)]) {
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

auto PatcherMeshShaderComplexMaterial::applyPatch(
    NiShape& nifShape, const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
{
    bool changed = false;

    // Apply shader
    changed |= applyShader(nifShape);

    // Check if specular should be white
    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_METALNESS)) {
        Logger::trace(L"Setting specular to white because CM has metalness");
        auto* nifShader = getNIF()->GetShader(&nifShape);
        auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, 1.0F);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, 1.0F);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, 1.0F);
    }

    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_GLOSSINESS)) {
        Logger::trace(L"Setting specular flag because CM has glossiness");
        auto* nifShader = getNIF()->GetShader(&nifShape);
        auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
    }

    // Apply slots
    applyPatchSlots(getTextureSet(nifShape), match, newSlots);
    changed |= setTextureSet(nifShape, newSlots);

    return changed;
}

auto PatcherMeshShaderComplexMaterial::applyPatchSlots(
    const NIFUtil::TextureSet& oldSlots, const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
{
    newSlots = oldSlots;

    const auto matchedPath = match.matchedPath;

    newSlots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)] = L"";
    newSlots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)] = matchedPath;

    const bool enableDynCubemaps
        = !(ParallaxGenDirectory::checkGlobMatchInVector(getNIFPath().wstring(), s_dynCubemapBlocklist)
            || ParallaxGenDirectory::checkGlobMatchInVector(matchedPath, s_dynCubemapBlocklist));

    static const auto dynCubemapPathSlashFix = boost::replace_all_copy(s_DYNCUBEMAPPATH.wstring(), L"/", L"\\");
    if (enableDynCubemaps) {
        newSlots[static_cast<size_t>(NIFUtil::TextureSlots::CUBEMAP)] = dynCubemapPathSlashFix;
    }

    return newSlots != oldSlots;
}

void PatcherMeshShaderComplexMaterial::processNewTXSTRecord(const PatcherMatch& match, const std::string& edid) { }

auto PatcherMeshShaderComplexMaterial::applyShader(NiShape& nifShape) -> bool
{
    bool changed = false;

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Remove texture slots if disabling MLP
    if (s_disableMLP && nifShaderBSLSP->GetShaderType() == BSLSP_MULTILAYERPARALLAX) {
        changed |= NIFUtil::setTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::GLOW, "");
        changed |= NIFUtil::setTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::MULTILAYER, "");
        changed |= NIFUtil::setTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::BACKLIGHT, "");

        changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    }

    // Set NIFShader type to env map
    changed |= NIFUtil::setShaderType(nifShader, BSLSP_ENVMAP);
    changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, 1.0F);
    changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, 1.0F);

    // Set NIFShader flags
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);

    return changed;
}
