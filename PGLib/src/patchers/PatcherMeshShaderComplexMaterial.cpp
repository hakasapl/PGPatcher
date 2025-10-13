#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include "ParallaxGenDirectory.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

// Statics
std::vector<wstring> PatcherMeshShaderComplexMaterial::s_dynCubemapBlocklist;

std::shared_mutex PatcherMeshShaderComplexMaterial::s_metaCacheMutex;
std::unordered_map<filesystem::path, nlohmann::json> PatcherMeshShaderComplexMaterial::s_metaCache;

auto PatcherMeshShaderComplexMaterial::loadStatics(const std::vector<std::wstring>& dynCubemapBlocklist) -> void
{
    PatcherMeshShaderComplexMaterial::s_dynCubemapBlocklist = dynCubemapBlocklist;
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

auto PatcherMeshShaderComplexMaterial::canApply(NiShape& nifShape, [[maybe_unused]] bool singlepassMATO) -> bool
{
    // Prep
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Get NIFShader type
    auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != BSLSP_DEFAULT && nifShaderType != BSLSP_ENVMAP && nifShaderType != BSLSP_PARALLAX
        && nifShaderType != BSLSP_MULTILAYERPARALLAX) {
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

    if (singlepassMATO
        && (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
            || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
            || NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING))) {
        Logger::trace(L"Shape Rejected: Singlepass MATO incompatible shader flags");
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

void PatcherMeshShaderComplexMaterial::applyPatch(
    NIFUtil::TextureSet& slots, NiShape& nifShape, const PatcherMatch& match)
{
    // Apply shader
    applyShader(nifShape);
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Check if specular should be white
    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_METALNESS)) {
        NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, 1.0F);
        NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, 1.0F);
        NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, 1.0F);
    }

    if (getPGD()->hasTextureAttribute(match.matchedPath, NIFUtil::TextureAttribute::CM_GLOSSINESS)) {
        NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
    }

    // Apply any extra meta overrides
    if (match.extraData != nullptr) {
        auto meta = *static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "specular_enabled" attribute
        if (meta.contains("specular_enabled") && meta["specular_enabled"].is_boolean()) {
            if (meta["specular_enabled"].get<bool>()) {
                NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            } else {
                NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            }
        }

        // "specular_color" attribute
        if (meta.contains("specular_color") && meta["specular_color"].is_array()
            && meta["specular_color"].size() == 3) {
            const auto specColor = meta["specular_color"];
            NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, specColor[0].get<float>());
            NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, specColor[1].get<float>());
            NIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, specColor[2].get<float>());
        }

        // "specular_strength" attribute
        if (meta.contains("specular_strength") && meta["specular_strength"].is_number()) {
            NIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, meta["specular_strength"].get<float>());
        }

        // "glosiness" attribute
        if (meta.contains("glossiness") && meta["glossiness"].is_number()) {
            NIFUtil::setShaderFloat(nifShaderBSLSP->glossiness, meta["glossiness"].get<float>());
        }

        // "environment_map_scale" attribute
        if (meta.contains("environment_map_scale") && meta["environment_map_scale"].is_number()) {
            NIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, meta["environment_map_scale"].get<float>());
        }
    }

    // Apply slots
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderComplexMaterial::applyPatchSlots(NIFUtil::TextureSet& slots, const PatcherMatch& match)
{
    const auto matchedPath = match.matchedPath;

    slots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)] = L"";
    slots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)] = matchedPath;

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
        slots[static_cast<size_t>(NIFUtil::TextureSlots::CUBEMAP)] = dynCubemapPathSlashFix;
    }
}

void PatcherMeshShaderComplexMaterial::applyShader(NiShape& nifShape)
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to env map
    NIFUtil::setShaderType(nifShader, BSLSP_ENVMAP);
    NIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, 1.0F);
    NIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, 1.0F);

    // Set NIFShader flags
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
}

auto PatcherMeshShaderComplexMaterial::getMaterialMeta(const filesystem::path& envMaskPath) -> nlohmann::json
{
    {
        const shared_lock lk(s_metaCacheMutex);
        if (s_metaCache.contains(envMaskPath)) {
            return s_metaCache.at(envMaskPath);
        }
    }

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

    const std::scoped_lock lk(s_metaCacheMutex);
    s_metaCache[envMaskPath] = meta;

    return meta;
}
