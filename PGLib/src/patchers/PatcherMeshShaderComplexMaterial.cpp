#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"

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
std::shared_mutex PatcherMeshShaderComplexMaterial::s_metaCacheMutex;
std::unordered_map<filesystem::path, nlohmann::json> PatcherMeshShaderComplexMaterial::s_metaCache;

auto PatcherMeshShaderComplexMaterial::getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshShader> {
        return make_unique<PatcherMeshShaderComplexMaterial>(nifPath, nif);
    };
}

auto PatcherMeshShaderComplexMaterial::getShaderType() -> PGEnums::ShapeShader
{
    return PGEnums::ShapeShader::COMPLEXMATERIAL;
}

PatcherMeshShaderComplexMaterial::PatcherMeshShaderComplexMaterial(filesystem::path nifPath,
                                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "ComplexMaterial")
{
}

auto PatcherMeshShaderComplexMaterial::canApply(NiShape& nifShape,
                                                [[maybe_unused]] bool singlepassMATO,
                                                const PGPlugin::ModelRecordType& modelRecordType) -> bool
{
    if (modelRecordType == PGPlugin::ModelRecordType::GRASS) {
        // grass is not supported
        return false;
    }

    // Prep
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Get NIFShader type
    auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != BSLSP_DEFAULT && nifShaderType != BSLSP_ENVMAP && nifShaderType != BSLSP_PARALLAX) {
        return false;
    }

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_ANISOTROPIC_LIGHTING)
        && (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING))) {
        return false;
    }

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
        && PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
        && PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING)) {
        return false;
    }

    if (singlepassMATO
        && (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING))) {
        return false;
    }

    return true;
}

auto PatcherMeshShaderComplexMaterial::shouldApply(nifly::NiShape& nifShape,
                                                   std::vector<PatcherMatch>& matches) -> bool
{
    // Check for CM matches
    return shouldApply(getTextureSet(getNIFPath(), *getNIF(), nifShape), matches);
}

auto PatcherMeshShaderComplexMaterial::shouldApply(const PGTypes::TextureSet& oldSlots,
                                                   std::vector<PatcherMatch>& matches) -> bool
{
    auto* pgd = PGGlobals::getPGD();
    auto* pgd3d = PGGlobals::getPGD3D();

    static const auto cmBaseMap = pgd->getTextureMapConst(PGEnums::TextureSlots::ENVMASK);

    matches.clear();

    // Search prefixes
    const auto searchPrefixes = PGNIFUtil::getSearchPrefixes(oldSlots);

    // Check if complex material file exists
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
        foundMatches
            = PGNIFUtil::getTexMatch(searchPrefixes.at(slot), PGEnums::TextureType::COMPLEXMATERIAL, cmBaseMap);

        if (!foundMatches.empty()) {
            matchedFromSlot = static_cast<PGEnums::TextureSlots>(slot);
            break;
        }
    }

    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (pgd3d->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;
            curMatch.matchedFrom.insert(matchedFromSlot);

            // get extra metadata and add to match extra data
            const auto meta = getMaterialMeta(match.path);
            if (!meta.is_null()) {
                curMatch.extraData = make_shared<decltype(nlohmann::json())>(meta);
            }

            if (match.path == oldSlots[static_cast<size_t>(PGEnums::TextureSlots::ENVMASK)]) {
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

void PatcherMeshShaderComplexMaterial::applyPatch(PGTypes::TextureSet& slots,
                                                  NiShape& nifShape,
                                                  const PatcherMatch& match)
{
    auto* pgd = PGGlobals::getPGD();

    // Apply shader
    applyShader(nifShape);
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Check if specular should be white
    if (pgd->hasTextureAttribute(match.matchedPath, PGEnums::TextureAttribute::CM_METALNESS)) {
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, 1.0F);
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, 1.0F);
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, 1.0F);
    }

    if (pgd->hasTextureAttribute(match.matchedPath, PGEnums::TextureAttribute::CM_GLOSSINESS)) {
        PGNIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
    }

    // Apply any extra meta overrides
    if (match.extraData != nullptr) {
        auto meta = *static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "specular_enabled" attribute
        if (meta.contains("specular_enabled") && meta["specular_enabled"].is_boolean()) {
            if (meta["specular_enabled"].get<bool>()) {
                PGNIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            } else {
                PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_SPECULAR);
            }
        }

        // "specular_color" attribute
        if (meta.contains("specular_color") && meta["specular_color"].is_array()
            && meta["specular_color"].size() == 3) {
            const auto specColor = meta["specular_color"];
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, specColor[0].get<float>());
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, specColor[1].get<float>());
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, specColor[2].get<float>());
        }

        // "specular_strength" attribute
        if (meta.contains("specular_strength") && meta["specular_strength"].is_number()) {
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, meta["specular_strength"].get<float>());
        }

        // "glosiness" attribute
        if (meta.contains("glossiness") && meta["glossiness"].is_number()) {
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->glossiness, meta["glossiness"].get<float>());
        }

        // "environment_map_scale" attribute
        if (meta.contains("environment_map_scale") && meta["environment_map_scale"].is_number()) {
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, meta["environment_map_scale"].get<float>());
        }
    }

    // Apply slots
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderComplexMaterial::applyPatchSlots(PGTypes::TextureSet& slots,
                                                       const PatcherMatch& match)
{
    const auto matchedPath = match.matchedPath;

    slots[static_cast<size_t>(PGEnums::TextureSlots::PARALLAX)] = L"";
    slots[static_cast<size_t>(PGEnums::TextureSlots::ENVMASK)] = matchedPath;

    // Apply any extra meta overrides
    bool enableDynCubemaps = true;
    if (match.extraData != nullptr) {
        auto meta = *static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "dynamic_cubemap" attribute
        if (meta.contains("dynamic_cubemap") && meta["dynamic_cubemap"].is_boolean()) {
            enableDynCubemaps = meta["dynamic_cubemap"].get<bool>();
        }
    }

    if (enableDynCubemaps) {
        slots[static_cast<size_t>(PGEnums::TextureSlots::CUBEMAP)] = s_DYNCUBEMAPPATH.wstring();
    }
}

void PatcherMeshShaderComplexMaterial::applyShader(NiShape& nifShape)
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to env map
    PGNIFUtil::setShaderType(nifShader, BSLSP_ENVMAP);
    PGNIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, 1.0F);
    PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, 1.0F);

    // Set NIFShader flags
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
}

auto PatcherMeshShaderComplexMaterial::getMaterialMeta(const filesystem::path& envMaskPath) -> nlohmann::json
{
    auto* pgd = PGGlobals::getPGD();

    {
        const shared_lock lk(s_metaCacheMutex);
        if (s_metaCache.contains(envMaskPath)) {
            return s_metaCache.at(envMaskPath);
        }
    }

    // find file path of meta, which is the envMaskPath extension replaced with .json
    auto metaPath = envMaskPath;
    metaPath.replace_extension(".json");

    if (!pgd->isFile(metaPath)) {
        return {};
    }

    // metadata file exists
    nlohmann::json meta;
    const auto jsonBytes = pgd->getFile(metaPath);
    if (!FileUtil::getJSONFromBytes(jsonBytes, meta)) {
        Logger::error(L"Failed to parse JSON: {}", metaPath.wstring());
        return {};
    }

    const std::scoped_lock lk(s_metaCacheMutex);
    s_metaCache[envMaskPath] = meta;

    return meta;
}
