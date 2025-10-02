#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include <Shaders.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <nlohmann/json_fwd.hpp>

#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

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
    return shouldApply(getTextureSet(getNIFPath(), *getNIF(), nifShape), matches);
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

            // get extra metadata and add to match extra data
            const auto meta = getMaterialMeta(match.path);
            if (!meta.is_null()) {
                curMatch.extraData = make_shared<decltype(nlohmann::json())>(meta);
            }

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
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Check if specular should be white
    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_METALNESS)) {
        Logger::trace(L"Setting specular to white because CM has metalness");
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, 1.0F);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, 1.0F);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, 1.0F);
    }

    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_GLOSSINESS)) {
        Logger::trace(L"Setting specular flag because CM has glossiness");
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
    }

    // Apply any extra meta overrides
    if (match.extraData != nullptr) {
        auto meta = *static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "specular_enabled" attribute
        if (meta.contains("specular_enabled") && meta["specular_enabled"].is_boolean()) {
            if (meta["specular_enabled"].get<bool>()) {
                changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            } else {
                changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            }
        }

        // "specular_color" attribute
        if (meta.contains("specular_color") && meta["specular_color"].is_array()
            && meta["specular_color"].size() == 3) {
            const auto specColor = meta["specular_color"];
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, specColor[0].get<float>());
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, specColor[1].get<float>());
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, specColor[2].get<float>());
        }

        // "specular_strength" attribute
        if (meta.contains("specular_strength") && meta["specular_strength"].is_number_float()) {
            changed
                |= NIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, meta["specular_strength"].get<float>());
        }

        // "glosiness" attribute
        if (meta.contains("glossiness") && meta["glossiness"].is_number_float()) {
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->glossiness, meta["glossiness"].get<float>());
        }

        // "environment_map_scale" attribute
        if (meta.contains("environment_map_scale") && meta["environment_map_scale"].is_number_float()) {
            changed |= NIFUtil::setShaderFloat(
                nifShaderBSLSP->environmentMapScale, meta["environment_map_scale"].get<float>());
        }
    }

    // Apply slots
    applyPatchSlots(getTextureSet(getNIFPath(), *getNIF(), nifShape), match, newSlots);
    changed |= setTextureSet(getNIFPath(), *getNIF(), nifShape, newSlots);

    return changed;
}

auto PatcherMeshShaderComplexMaterial::applyPatchSlots(
    const NIFUtil::TextureSet& oldSlots, const PatcherMatch& match, NIFUtil::TextureSet& newSlots) -> bool
{
    newSlots = oldSlots;

    const auto matchedPath = match.matchedPath;

    newSlots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)] = L"";
    newSlots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)] = matchedPath;

    // Apply any extra meta overrides
    bool enableDynCubemaps
        = !(ParallaxGenDirectory::checkGlobMatchInVector(getNIFPath().wstring(), s_dynCubemapBlocklist)
            || ParallaxGenDirectory::checkGlobMatchInVector(matchedPath, s_dynCubemapBlocklist));

    if (match.extraData != nullptr) {
        auto meta = *static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "dynamic_cubemap" attribute
        if (meta.contains("dynamic_cubemap") && meta["dynamic_cubemap"].is_boolean()) {
            enableDynCubemaps = meta["dynamic_cubemap"].get<bool>();
        }
    }

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

auto PatcherMeshShaderComplexMaterial::getMaterialMeta(const filesystem::path& envMaskPath) -> nlohmann::json
{
    // find file path of meta, which is the envMaskPath extension replaced with .json
    auto metaPath = envMaskPath;
    metaPath.replace_extension(".json");

    if (!getPGD()->isFile(metaPath)) {
        return {};
    }

    // metadata file exists
    nlohmann::json meta;
    const auto jsonBytes = getPGD()->getFile(metaPath);
    if (!ParallaxGenUtil::getJSONFromBytes(jsonBytes, meta)) {
        Logger::error(L"Failed to parse CM metadata JSON: {}", metaPath.wstring());
        return {};
    }

    return meta;
}
