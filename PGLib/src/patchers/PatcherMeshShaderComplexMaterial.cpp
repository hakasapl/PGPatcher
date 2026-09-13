#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Statics.
std::shared_mutex PatcherMeshShaderComplexMaterial::s_metaCacheMutex;
std::unordered_map<std::filesystem::path, nlohmann::json> PatcherMeshShaderComplexMaterial::s_metaCache;

auto PatcherMeshShaderComplexMaterial::factory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshShader> {
        return std::make_unique<PatcherMeshShaderComplexMaterial>(nifPath, nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderComplexMaterial::shaderType() { return PGEnums::ShapeShader::ComplexMaterial; }

void PatcherMeshShaderComplexMaterial::loadOptions(std::unordered_map<std::string,
                                                                      std::string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr)
        if (option == "disable_dyncubemap")
            s_disableDynCubemap = true;
}

void PatcherMeshShaderComplexMaterial::loadOptions(bool disableDynCubemap) { s_disableDynCubemap = disableDynCubemap; }

PatcherMeshShaderComplexMaterial::PatcherMeshShaderComplexMaterial(std::filesystem::path nifPath,
                                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "ComplexMaterial")
{
}

bool PatcherMeshShaderComplexMaterial::canApply(nifly::NiShape& nifShape,
                                                [[maybe_unused]] bool isSinglepassMATO,
                                                const PGPlugin::ModelRecordType& modelRecordType)
{
    if (modelRecordType == PGPlugin::ModelRecordType::Grass) {
        // Grass is not supported.
        return false;
    }

    // Prep.
    auto* nifShader = nif()->GetShader(&nifShape);
    const auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Get NIFShader type.
    const auto nifShaderType = static_cast<nifly::BSLightingShaderPropertyShaderType>(nifShader->GetShaderType());
    if (nifShaderType != nifly::BSLSP_DEFAULT && nifShaderType != nifly::BSLSP_ENVMAP
        && nifShaderType != nifly::BSLSP_PARALLAX)
        return false;

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_ANISOTROPIC_LIGHTING)
        && (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING))) {
        return false;
    }

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING)
        && PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING)
        && PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING)) {
        return false;
    }

    if (isSinglepassMATO
        && (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING)
            || PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING))) {
        return false;
    }

    return true;
}

bool PatcherMeshShaderComplexMaterial::shouldApply(nifly::NiShape& nifShape,
                                                   std::vector<PatcherMatch>& matches)
{
    // Check for CM matches.
    return shouldApply(textureSet(nifPath(), *nif(), nifShape), matches);
}

bool PatcherMeshShaderComplexMaterial::shouldApply(const PGTypes::TextureSet& oldSlots,
                                                   std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();
    auto* pgd3d = PGGlobals::pGD3D();

    static const auto cmBaseMap = pgd->textureMapConst(PGEnums::TextureSlots::EnvMask);

    matches.clear();

    // Search prefixes.
    const auto searchPrefixes = PGNIFUtil::searchPrefixes(oldSlots);

    // Check if complex material file exists.
    static const std::vector<int> slotSearch = { 1, 0 }; // Diffuse first, then normal
    std::filesystem::path baseMap;
    std::vector<PGTypes::PGTexture> foundMatches;
    for (const int& slot : slotSearch) {
        baseMap = oldSlots.at(slot);
        if (baseMap.empty() || !pgd->isFile(baseMap))
            continue;

        foundMatches.clear();
        foundMatches = PGNIFUtil::texMatch(searchPrefixes.at(slot), PGEnums::TextureType::ComplexMaterial, cmBaseMap);

        if (!foundMatches.empty())
            break;
    }

    PatcherMatch lastMatch; // Variable to store the match that equals OldSlots[Slot], if found
    for (const auto& match : foundMatches) {
        if (pgd3d->checkIfAspectRatioMatches(baseMap, match.path)) {
            PatcherMatch curMatch;
            curMatch.matchedPath = match.path;

            // Get extra metadata and add to match extra data.
            const auto meta = materialMeta(match.path);
            if (!meta.is_null())
                curMatch.extraData = std::make_shared<decltype(nlohmann::json())>(meta);

            if (match.path == oldSlots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)])
                lastMatch = curMatch; // Save the match that equals OldSlots[Slot]
            else
                matches.push_back(curMatch); // Add other matches
        }
    }

    if (!lastMatch.matchedPath.empty())
        matches.push_back(lastMatch); // Add the match that equals OldSlots[Slot]

    return !matches.empty();
}

void PatcherMeshShaderComplexMaterial::applyPatch(PGTypes::TextureSet& slots,
                                                  nifly::NiShape& nifShape,
                                                  const PatcherMatch& match)
{
    auto* pgd = PGGlobals::pgd();

    // Apply shader.
    applyShader(nifShape);
    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Check if specular should be white.
    if (pgd->hasTextureAttribute(match.matchedPath, PGEnums::TextureAttribute::CMMetalness)) {
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, 1);
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, 1);
        PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, 1);
    }

    if (pgd->hasTextureAttribute(match.matchedPath, PGEnums::TextureAttribute::CMGlossiness))
        PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF1_SPECULAR);

    // Apply any extra meta overrides.
    if (match.extraData) {
        auto meta = *std::static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "specular_enabled" attribute.
        if (meta.contains("specular_enabled") && meta["specular_enabled"].is_boolean()) {
            if (meta["specular_enabled"].get<bool>())
                PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF1_SPECULAR);
            else
                PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_SPECULAR);
        }

        // "specular_color" attribute.
        if (meta.contains("specular_color") && meta["specular_color"].is_array()
            && meta["specular_color"].size() == 3) {
            const auto specColor = meta["specular_color"];
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.x, specColor[0].get<float>());
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.y, specColor[1].get<float>());
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularColor.z, specColor[2].get<float>());
        }

        // "specular_strength" attribute.
        if (meta.contains("specular_strength") && meta["specular_strength"].is_number())
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, meta["specular_strength"].get<float>());

        // "glosiness" attribute.
        if (meta.contains("glossiness") && meta["glossiness"].is_number())
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->glossiness, meta["glossiness"].get<float>());

        // "environment_map_scale" attribute.
        if (meta.contains("environment_map_scale") && meta["environment_map_scale"].is_number())
            PGNIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, meta["environment_map_scale"].get<float>());
    }

    // Apply slots.
    applyPatchSlots(slots, match);
}

void PatcherMeshShaderComplexMaterial::applyPatchSlots(PGTypes::TextureSet& slots,
                                                       const PatcherMatch& match)
{
    const auto matchedPath = match.matchedPath;

    slots[static_cast<size_t>(PGEnums::TextureSlots::Parallax)] = L"";
    slots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)] = matchedPath;

    if (s_disableDynCubemap)
        return;

    // Apply any extra meta overrides.
    bool enableDynCubemaps = true;
    if (match.extraData) {
        auto meta = *std::static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

        // "dynamic_cubemap" attribute.
        if (meta.contains("dynamic_cubemap") && meta["dynamic_cubemap"].is_boolean())
            enableDynCubemaps = meta["dynamic_cubemap"].get<bool>();
    }

    if (enableDynCubemaps)
        slots[static_cast<size_t>(PGEnums::TextureSlots::Cubemap)] = s_dynCubemapPath.wstring();
}

void PatcherMeshShaderComplexMaterial::applyShader(nifly::NiShape& nifShape)
{
    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Set NIFShader type to env map.
    PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_ENVMAP);
    PGNIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, 1);
    PGNIFUtil::setShaderFloat(nifShaderBSLSP->specularStrength, 1);

    // Set NIFShader flags.
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
}

uint64_t PatcherMeshShaderComplexMaterial::matchExtraDataHash(const PatcherMatch& match) const
{
    if (!match.extraData)
        return 0;

    const auto meta = std::static_pointer_cast<decltype(nlohmann::json())>(match.extraData);

    HashUtil::Fnv1a64 hasher;
    hasher.add(meta->dump());
    return hasher.value();
}

nlohmann::json PatcherMeshShaderComplexMaterial::materialMeta(const std::filesystem::path& envMaskPath)
{
    auto* pgd = PGGlobals::pgd();

    {
        const std::shared_lock lk(s_metaCacheMutex);
        if (s_metaCache.contains(envMaskPath))
            return s_metaCache.at(envMaskPath);
    }

    // Find file path of meta, which is the envMaskPath extension replaced with .json.
    auto metaPath = envMaskPath;
    metaPath.replace_extension(".json");

    if (!pgd->isFile(metaPath))
        return { };

    // Metadata file exists.
    nlohmann::json meta;
    const auto jsonBytes = pgd->file(metaPath);
    if (!FileUtil::getJSONFromBytes(jsonBytes, meta)) {
        Logger::error(L"Failed to parse JSON: {}", metaPath.wstring());
        return { };
    }

    const std::scoped_lock lk(s_metaCacheMutex);
    s_metaCache[envMaskPath] = meta;

    return meta;
}
