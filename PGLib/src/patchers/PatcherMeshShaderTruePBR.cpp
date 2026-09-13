#include "patchers/PatcherMeshShaderTruePBR.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/HashUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include "VertexData.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/gil.hpp>
#include <boost/gil/color_convert.hpp>
#include <boost/gil/extension/toolbox/color_converters.hpp>
#include <boost/gil/extension/toolbox/color_spaces/hsl.hpp>
#include <boost/gil/typedefs.hpp>
#include <mutex>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

PatcherMeshShaderTruePBR::PatcherMeshShaderTruePBR(std::filesystem::path nifPath,
                                                   nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath),
                        nif,
                        "TruePBR")
{
}

std::map<size_t,
         nlohmann::json>&
PatcherMeshShaderTruePBR::truePBRConfigs()
{
    static std::map<size_t, nlohmann::json> truePBRConfigs = { };
    return truePBRConfigs;
}

std::map<size_t,
         nlohmann::json>&
PatcherMeshShaderTruePBR::pathLookupJSONs()
{
    static std::map<size_t, nlohmann::json> pathLookupJSONs = { };
    return pathLookupJSONs;
}

std::map<std::wstring,
         std::vector<size_t>>&
PatcherMeshShaderTruePBR::truePBRDiffuseInverse()
{
    static std::map<std::wstring, std::vector<size_t>> truePBRDiffuseInverse = { };
    return truePBRDiffuseInverse;
}

std::map<std::wstring,
         std::vector<size_t>>&
PatcherMeshShaderTruePBR::truePBRNormalInverse()
{
    static std::map<std::wstring, std::vector<size_t>> truePBRNormalInverse = { };
    return truePBRNormalInverse;
}

std::unordered_map<PGEnums::TextureSlots,
                   std::unordered_map<std::wstring,
                                      std::vector<size_t>>>&
PatcherMeshShaderTruePBR::truePBRMatchXMap()
{
    static std::unordered_map<PGEnums::TextureSlots, std::unordered_map<std::wstring, std::vector<size_t>>>
        truePBRMatchXMap = { };
    return truePBRMatchXMap;
}

auto PatcherMeshShaderTruePBR::pathLookupCache() -> std::unordered_map<std::tuple<std::wstring,
                                                                                  std::wstring>,
                                                                       bool,
                                                                       TupleStrHash>&
{
    static std::unordered_map<std::tuple<std::wstring, std::wstring>, bool, TupleStrHash> pathLookupCache = { };
    return pathLookupCache;
}

std::mutex& PatcherMeshShaderTruePBR::pathLookupCacheMutex()
{
    static std::mutex cacheMutex;
    return cacheMutex;
}

std::vector<std::string> PatcherMeshShaderTruePBR::truePBRConfigFilenameFields()
{
    static const std::vector<std::string> pgConfigFilenameFields = { "match_normal", "match_diffuse", "rename" };
    return pgConfigFilenameFields;
}

// Statics.
void PatcherMeshShaderTruePBR::loadStatics(const std::vector<std::filesystem::path>& pbrJSONs)
{
    auto* pgd = PGGlobals::pgd();

    size_t configOrder = 0;
    for (const auto& config : pbrJSONs) {
        // Check if Config is valid.
        auto configFileBytes = pgd->file(config);
        std::string configFileStr;
        std::ranges::transform(
            configFileBytes, std::back_inserter(configFileStr), [](std::byte b) { return static_cast<char>(b); });

        try {
            nlohmann::json j = nlohmann::json::parse(configFileStr);
            nlohmann::json jDefaults;
            nlohmann::json jEntries;

            // Check if j is a json object.
            if (j.is_object()) {
                if (!j.contains("default") || !j.contains("entries"))
                    continue;

                jDefaults = j["default"];
                jEntries = j["entries"];
            } else {
                jDefaults = nlohmann::json::object();
                jEntries = j;
            }

            // Loop through each Element.
            for (auto& element : jEntries) {
                // Merge defaults with element.
                for (const auto& [key, value] : jDefaults.items())
                    if (!element.contains(key))
                        element[key] = value;

                // Preprocessing steps here.
                if (element.contains("texture"))
                    element["match_diffuse"] = element["texture"];

                element["json"] = StringUtil::utf16toUTF8(config.wstring());

                // Loop through filename Fields.
                for (const auto& field : truePBRConfigFilenameFields())
                    if (element.contains(field) && !boost::istarts_with(element[field].get<std::string>(), "\\"))
                        element[field] = element[field].get<std::string>().insert(0, 1, '\\');

                Logger::trace(L"TruePBR Config {} Loaded: {}", configOrder, StringUtil::utf8toUTF16(element.dump()));
                truePBRConfigs()[configOrder++] = element;
            }
        } catch (nlohmann::json::parse_error& e) {
            Logger::debug(L"Failed to parse TruePBR config JSON: {}. Error: {}",
                          config.wstring(),
                          StringUtil::utf8toUTF16(e.what()));
            Logger::error(L"Failed to parse JSON: {}", config.wstring());
            continue;
        }
    }

    Logger::info(L"Found {} TruePBR entries", truePBRConfigs().size());

    // Create helper vectors.
    for (const auto& config : truePBRConfigs()) {
        // "match_normal" attribute.
        if (config.second.contains("match_normal")) {
            auto revNormal = StringUtil::utf8toUTF16(config.second["match_normal"].get<std::string>());
            revNormal = PGNIFUtil::texBase(revNormal);
            std::ranges::reverse(revNormal);

            truePBRNormalInverse()[StringUtil::toLowerASCIIFast(revNormal)].push_back(config.first);
        }

        // "match_diffuse" attribute.
        if (config.second.contains("match_diffuse")) {
            auto revDiffuse = StringUtil::utf8toUTF16(config.second["match_diffuse"].get<std::string>());
            revDiffuse = PGNIFUtil::texBase(revDiffuse);
            std::ranges::reverse(revDiffuse);

            truePBRDiffuseInverse()[StringUtil::toLowerASCIIFast(revDiffuse)].push_back(config.first);
        }

        // "path_contains" attribute.
        if (config.second.contains("path_contains"))
            pathLookupJSONs()[config.first] = config.second;

        // "matchX" attribute.
        for (int i = 0; i < numTextureSlots - 1; i++) {
            const std::string matchXStr = "match" + std::to_string(i + 1);
            if (config.second.contains(matchXStr)) {
                auto matchStr = StringUtil::utf8toUTF16(config.second.at(matchXStr).get<std::string>());

                // Prepend "textures\\" if it's not already there.
                if (!matchStr.empty() && !matchStr.starts_with(L"textures\\"))
                    matchStr.insert(0, L"textures\\");

                truePBRMatchXMap()[static_cast<PGEnums::TextureSlots>(i)][StringUtil::toLowerASCIIFast(matchStr)]
                    .push_back(config.first);
            }
        }
    }
}

auto PatcherMeshShaderTruePBR::factory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshShader> {
        return std::make_unique<PatcherMeshShaderTruePBR>(nifPath, nif);
    };
}

PGEnums::ShapeShader PatcherMeshShaderTruePBR::shaderType() { return PGEnums::ShapeShader::TruePBR; }

bool PatcherMeshShaderTruePBR::canApply([[maybe_unused]] nifly::NiShape& nifShape,
                                        [[maybe_unused]] bool isSinglepassMATO,
                                        [[maybe_unused]] const PGPlugin::ModelRecordType& modelRecordType)
{
    return true;
}

bool PatcherMeshShaderTruePBR::shouldApply(nifly::NiShape& nifShape,
                                           std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();

    // Prep.
    auto* nifShader = nif()->GetShader(&nifShape);
    const auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    matches.clear();

    // Find Old Slots.
    auto oldSlots = textureSet(nifPath(), *nif(), nifShape);

    shouldApply(oldSlots, matches);

    if (PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01)) {
        // Check if RMAOS exists.
        const auto& rmaosPath = oldSlots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)];
        if (!rmaosPath.empty() && pgd->isFile(rmaosPath)) {
            PatcherMatch match;
            match.matchedPath = nifPath().wstring();
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

bool PatcherMeshShaderTruePBR::shouldApply(const PGTypes::TextureSet& oldSlots,
                                           std::vector<PatcherMatch>& matches)
{
    auto* pgd = PGGlobals::pgd();

    // Get search prefixes.
    auto searchPrefixes = PGNIFUtil::searchPrefixes(oldSlots, false);
    // Only normal map gets _n part removed to match properly.
    searchPrefixes[1] = PGNIFUtil::texBase(oldSlots[1], PGEnums::TextureSlots::Normal);

    // Remove "pbr" part if starts with "textures\\pbr" for each search prefix.
    static constexpr size_t texturePBRStrLength = 13; // length of "textures\pbr\"
    for (auto& prefix : searchPrefixes)
        if (StringUtil::toLowerASCIIFast(prefix).starts_with(L"textures\\pbr\\"))
            prefix.replace(0, texturePBRStrLength, L"textures\\");

    std::map<size_t, std::tuple<nlohmann::json, std::wstring>> truePBRData;
    // "match_normal" attribute: Binary search for normal map.
    getSlotMatch(truePBRData, searchPrefixes[1], truePBRNormalInverse(), nifPath().wstring());

    // "match_diffuse" attribute: Binary search for diffuse map.
    getSlotMatch(truePBRData, searchPrefixes[0], truePBRDiffuseInverse(), nifPath().wstring());

    // "path_contains" attribute: Linear search for path_contains.
    getPathContainsMatch(truePBRData, searchPrefixes[0], nifPath().wstring());

    // "matchX" attribute: search exact match for each slot.
    getMatchXMatch(truePBRData, oldSlots, nifPath().wstring());

    // Split data into individual JSONs.
    std::unordered_map<std::wstring, std::map<size_t, std::tuple<nlohmann::json, std::wstring>>> truePBROutputData;
    for (const auto& [sequence, data] : truePBRData) {
        // Get current JSON.
        const auto matchedPath = StringUtil::utf8toUTF16(std::get<0>(data)["json"].get<std::string>());

        // Add to output.
        if (!truePBROutputData.contains(matchedPath)) {
            // If MatchedPath doesn't exist, insert it with an empty map and then add Sequence, Data.
            truePBROutputData.emplace(matchedPath, std::map<size_t, std::tuple<nlohmann::json, std::wstring>> { });
        }
        truePBROutputData[matchedPath][sequence] = data;
    }

    // Convert output to vectors.
    for (auto& [json, jsonData] : truePBROutputData) {
        PatcherMatch match;
        match.matchedPath = json;
        match.extraData = std::make_shared<decltype(jsonData)>(jsonData);

        // Loop through json data.
        bool deleteShape = false;
        for (const auto& [sequence, data] : jsonData) {
            if (std::get<0>(data).contains("delete") && std::get<0>(data)["delete"].is_boolean()
                && std::get<0>(data)["delete"].get<bool>()) {
                // Marked for deletion, skip slot checks.
                deleteShape = true;
                break;
            }
        }

        // Check paths.
        bool valid = true;

        if (!deleteShape) {
            PGTypes::TextureSet newSlots = oldSlots;
            applyPatchSlots(newSlots, match);
            for (size_t i = 0; i < numTextureSlots; i++) {
                if (!newSlots.at(i).empty() && !pgd->isFile(newSlots.at(i))) {
                    // Slot does not exist.
                    if (s_printNonExistentPaths) {
                        Logger::warn(
                            L"Texture \"{}\" does not exist from PBR json \"{}\" when patching mesh \"{}\" (Skipping)",
                            newSlots.at(i),
                            match.matchedPath,
                            nifPath().wstring());
                    }

                    // Only invalidate if checkpaths is false.
                    if (s_checkPaths)
                        valid = false;
                }
            }
        }

        if (!valid)
            continue;

        matches.push_back(match);
    }

    // Sort matches by ExtraData key minimum value (this preserves order of JSONs to be 0 having priority if mod order.
    // Does not exist).
    std::ranges::sort(matches, [](const PatcherMatch& a, const PatcherMatch& b) {
        return std::get<0>(
                   *std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(a.extraData)
                        ->begin())
            > std::get<0>(
                   *std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(b.extraData)
                        ->begin());
    });

    // Check for pre-patch case.
    if (truePBRData.empty()) {
        const auto& rmaosPath = oldSlots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)];
        // If not start with PBR add it for the check.
        if (pgd->textureType(rmaosPath) == PGEnums::TextureType::RMAOS) {
            // Found RMAOS without json.
            PatcherMatch match;
            match.matchedPath = rmaosPath;
            match.extraData = nullptr;
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

void PatcherMeshShaderTruePBR::getSlotMatch(std::map<size_t,
                                                     std::tuple<nlohmann::json,
                                                                std::wstring>>& truePBRData,
                                            const std::wstring& texName,
                                            const std::map<std::wstring,
                                                           std::vector<size_t>>& lookup,
                                            const std::wstring& nifPath)
{
    // Binary search for map.
    auto mapReverse = StringUtil::toLowerASCIIFast(texName);
    std::ranges::reverse(mapReverse);
    auto it = lookup.lower_bound(mapReverse);

    // Get the first element of the reverse path.
    auto reverseFile = mapReverse;
    const auto pos = reverseFile.find_first_of(L'\\');
    if (pos != std::wstring::npos)
        reverseFile = reverseFile.substr(0, pos);

    // Check if match is 1 back.
    if (it != lookup.begin() && boost::starts_with(prev(it)->first, reverseFile)) {
        it = prev(it);
    } else if (it != lookup.end() && boost::starts_with(it->first, reverseFile)) {
        // Check if match is current iterator, just continue here.
    } else {
        // No match found.
        return;
    }

    auto beginIt = it;
    while (beginIt != lookup.begin())
        if (boost::starts_with(prev(beginIt)->first, reverseFile))
            beginIt = prev(beginIt);
        else
            break;

    // Initialize CFG set.
    std::set<size_t> cfgs;

    // Create vector of all matches based on beginIt and It.
    while (beginIt != next(it)) {
        if (!boost::starts_with(mapReverse, beginIt->first)) {
            // Not a valid match.
            beginIt = next(beginIt);
            continue;
        }

        cfgs.insert(beginIt->second.begin(), beginIt->second.end());
        beginIt = next(beginIt);
    }

    if (cfgs.empty())
        return;

    // Loop through all matches.
    for (const auto& cfg : cfgs)
        insertTruePBRData(truePBRData, texName, cfg, nifPath);
}

void PatcherMeshShaderTruePBR::getPathContainsMatch(std::map<size_t,
                                                             std::tuple<nlohmann::json,
                                                                        std::wstring>>& truePBRData,
                                                    const std::wstring& diffuse,
                                                    const std::wstring& nifPath)
{
    // "patch_contains" attribute: Linear search for path_contains.
    auto& cache = pathLookupCache();
    auto& cacheMutex = pathLookupCacheMutex();

    // Check for path_contains only if no name match because it's a O(n) operation
    for (const auto& config : pathLookupJSONs()) {
        // Check if in cache.
        auto cacheKey
            = std::make_tuple(StringUtil::utf8toUTF16(config.second["path_contains"].get<std::string>()), diffuse);

        bool pathMatch = false;
        {
            const std::scoped_lock lock(cacheMutex);
            if (!cache.contains(cacheKey)) {
                // Not in cache, update it.
                cache[cacheKey] = boost::icontains(diffuse, std::get<0>(cacheKey));
            }
            pathMatch = cache[cacheKey];
        }

        if (pathMatch)
            insertTruePBRData(truePBRData, diffuse, config.first, nifPath);
    }
}

void PatcherMeshShaderTruePBR::getMatchXMatch(std::map<size_t,
                                                       std::tuple<nlohmann::json,
                                                                  std::wstring>>& truePBRData,
                                              const PGTypes::TextureSet& oldSlots,
                                              const std::wstring& nifPath)
{
    const auto& truePBRMatchXMap = PatcherMeshShaderTruePBR::truePBRMatchXMap();
    for (size_t i = 0; i < numTextureSlots - 1; i++) {
        const auto curSlot = static_cast<PGEnums::TextureSlots>(i);

        // Check if this slot was ever cached.
        if (!truePBRMatchXMap.contains(curSlot))
            continue;

        // Get texture slot str.
        const auto& lookupWStr = oldSlots.at(i);
        if (lookupWStr.empty())
            continue;
        const auto lookupStr = StringUtil::toLowerASCIIFast(lookupWStr);

        // Lookup.
        const auto& matchXMap = truePBRMatchXMap.at(curSlot);
        if (!matchXMap.contains(lookupStr))
            continue;

        // Add to truePBRData.
        for (const auto& cfg : matchXMap.at(lookupStr))
            insertTruePBRData(truePBRData, PGNIFUtil::texBase(lookupStr, curSlot), cfg, nifPath);
    }
}

void PatcherMeshShaderTruePBR::insertTruePBRData(std::map<size_t,
                                                          std::tuple<nlohmann::json,
                                                                     std::wstring>>& truePBRData,
                                                 const std::wstring& texName,
                                                 size_t cfg,
                                                 const std::wstring& nifPath)
{
    auto curCfg = truePBRConfigs()[cfg];

    // Check if we should skip this due to nif filter (this is expsenive, so we do it last).
    if (curCfg.contains("nif_filter") && !boost::icontains(nifPath, curCfg["nif_filter"].get<std::string>()))
        return;

    // Find and check prefix value.
    // Add the PBR part to the texture path.
    auto texPath = texName;
    const auto texPathLower = StringUtil::toLowerASCIIFast(texPath);
    if (texPathLower.starts_with(L"textures\\") && !texPathLower.starts_with(L"textures\\pbr\\"))
        texPath.replace(0, textureStrLength, L"textures\\pbr\\");

    // Get PBR path, which is the path without the matched field.
    std::wstring matchedField;
    if (curCfg.contains("match_normal") || curCfg.contains("match_diffuse")) {
        matchedField = curCfg.contains("match_normal") ? PGNIFUtil::texBase(curCfg["match_normal"].get<std::string>())
                                                       : PGNIFUtil::texBase(curCfg["match_diffuse"].get<std::string>());
    } else {
        // This is a "matchX" entry, so we can just use the whole texture path as is.
        matchedField = texPath;
    }
    texPath.erase(texPath.length() - matchedField.length(), matchedField.length());

    // "rename" attribute.
    if (curCfg.contains("rename")) {
        const auto renameField = curCfg["rename"].get<std::string>();
        if (!StringUtil::asciiFastIEquals(renameField, matchedField))
            matchedField = StringUtil::utf8toUTF16(renameField);
    }

    // PBR prefix path for the shape. PBR is always enabled for a matched entry: the legacy "pbr" JSON field is
    // ignored if present.
    const std::wstring matchedPath = StringUtil::toLowerASCIIFast(texPath + matchedField);

    truePBRData.insert({ cfg, { curCfg, matchedPath } });
}

void PatcherMeshShaderTruePBR::applyPatch(PGTypes::TextureSet& slots,
                                          nifly::NiShape& nifShape,
                                          const PatcherMatch& match)
{
    if (!match.extraData) {
        // No extra data, so this is a pre-patched mesh, do nothing.
        return;
    }

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);
    for (const auto& [sequence, data] : *extraData) {
        // Apply one patch.
        auto truePBRData = std::get<0>(data);
        const auto matchedPath = std::get<1>(data);
        applyOnePatch(&nifShape, truePBRData, matchedPath, slots);
    }
}

void PatcherMeshShaderTruePBR::applyPatchSlots(PGTypes::TextureSet& slots,
                                               const PatcherMatch& match)
{
    if (!match.extraData)
        return;

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);
    for (const auto& [sequence, data] : *extraData) {
        const auto truePBRData = std::get<0>(data);
        const auto matchedPath = std::get<1>(data);
        applyOnePatchSlots(slots, truePBRData, matchedPath);
    }
}

void PatcherMeshShaderTruePBR::applyShader(nifly::NiShape& nifShape)
{
    // Contrary to the other patchers, this one is generic and is not called normally other than setting for plugins,
    // later material swaps in CS are used.

    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // Set default PBR shader type.
    PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_DEFAULT);
    PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);

    // Clear unused flags.
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_HAIR_SOFT_LIGHTING);
    PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_DETAIL_MAP);
}

uint64_t PatcherMeshShaderTruePBR::matchExtraDataHash(const PatcherMatch& match) const
{
    if (!match.extraData)
        return 0;

    const auto extraData
        = std::static_pointer_cast<std::map<size_t, std::tuple<nlohmann::json, std::wstring>>>(match.extraData);

    // Config indices depend on the global order of PBR JSONs, which can shift when JSONs are added or removed.
    // Without changing how this shape is patched. Only the content and the relative order matter, so hash the entries.
    // In map (application) order without their indices.
    HashUtil::Fnv1a64 hasher;
    hasher.add(static_cast<uint64_t>(extraData->size()));
    for (const auto& [sequence, data] : *extraData) {
        hasher.add(std::get<0>(data).dump());
        hasher.add(std::get<1>(data));
    }

    return hasher.value();
}

void PatcherMeshShaderTruePBR::loadOptions(std::unordered_map<std::string,
                                                              std::string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "no_path_check")
            s_checkPaths = false;

        if (option == "print_nonexistent_paths")
            s_printNonExistentPaths = true;
    }
}

void PatcherMeshShaderTruePBR::loadOptions(const bool& checkPaths,
                                           const bool& shouldPrintNonExistentPaths)
{
    s_checkPaths = checkPaths;
    s_printNonExistentPaths = shouldPrintNonExistentPaths;
}

bool PatcherMeshShaderTruePBR::applyOnePatch(nifly::NiShape* nifShape,
                                             nlohmann::json& truePBRData,
                                             const std::wstring& matchedPath,
                                             PGTypes::TextureSet& newSlots)
{
    bool isChanged = false;

    // Prep.
    auto* nifShader = nif()->GetShader(nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);

    // "delete" attribute.
    if (truePBRData.contains("delete") && truePBRData["delete"].is_boolean() && truePBRData["delete"]) {
        nif()->DeleteShape(nifShape);
        isChanged = true;
        return isChanged;
    }

    // "smooth_angle" attribute.
    if (truePBRData.contains("smooth_angle") && truePBRData["smooth_angle"].is_number()) {
        nif()->CalcNormalsForShape(nifShape, true, true, truePBRData["smooth_angle"]);
        nif()->CalcTangentsForShape(nifShape);
        isChanged = true;
    }

    // "auto_uv" attribute.
    if (truePBRData.contains("auto_uv") && truePBRData["auto_uv"].is_number()) {
        std::vector<nifly::Triangle> tris;
        nifShape->GetTriangles(tris);
        const auto newUVScale = autoUVScale(nif()->GetUvsForShape(nifShape), nif()->GetVertsForShape(nifShape), tris)
            / truePBRData["auto_uv"];
        isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // "vertex_colors" attribute.
    if (truePBRData.contains("vertex_colors") && truePBRData["vertex_colors"].is_boolean()) {
        const auto newVertexColors = truePBRData["vertex_colors"].get<bool>();
        if (nifShape->HasVertexColors() != newVertexColors) {
            nifShape->SetVertexColors(newVertexColors);
            isChanged = true;
        }

        if (nifShader->HasVertexColors() != newVertexColors) {
            nifShader->SetVertexColors(newVertexColors);
            isChanged = true;
        }
    }

    // "vertex_color_lum_mult" and "vertex_color_sat_mult" attribute.
    if (nifShape->HasVertexColors()
        && ((truePBRData.contains("vertex_color_lum_mult") && truePBRData["vertex_color_lum_mult"].is_number())
            || truePBRData.contains("vertex_color_sat_mult") && truePBRData["vertex_color_sat_mult"].is_number())) {
        std::vector<nifly::BSVertexData>* vertData = nullptr;
        if (dynamic_cast<nifly::BSTriShape*>(nifShape))
            vertData = &dynamic_cast<nifly::BSTriShape*>(nifShape)->vertData;
        else if (dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape))
            vertData = &dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape)->vertData;

        if (vertData) {
            for (auto& vert : *vertData) {
                // Convert to HSL and multiply luminance then convert back.
                boost::gil::rgb8_pixel_t vertRGB(vert.colorData[0], vert.colorData[1], vert.colorData[2]);
                boost::gil::hsl32f_pixel_t vertHSL;
                boost::gil::color_convert(vertRGB, vertHSL);

                float newLVal = vertHSL[2];
                if (truePBRData.contains("vertex_color_lum_mult") && truePBRData["vertex_color_lum_mult"].is_number()) {
                    const auto newVertexColorMult = truePBRData["vertex_color_lum_mult"].get<float>();
                    newLVal = 1 - ((1 - vertHSL[2]) * newVertexColorMult);
                }

                float newSVal = vertHSL[1];
                if (truePBRData.contains("vertex_color_sat_mult") && truePBRData["vertex_color_sat_mult"].is_number()) {
                    const auto newVertexColorMult = truePBRData["vertex_color_sat_mult"].get<float>();
                    newSVal = vertHSL[1] * newVertexColorMult;
                }

                vertHSL[1] = std::clamp(newSVal, 0.0F, 1.0F);
                vertHSL[2] = std::clamp(newLVal, 0.0F, 1.0F);
                boost::gil::color_convert(vertHSL, vertRGB);

                if (vert.colorData[0] != vertRGB[0]) {
                    vert.colorData[0] = vertRGB[0];
                    isChanged = true;
                }

                if (vert.colorData[1] != vertRGB[1]) {
                    vert.colorData[1] = vertRGB[1];
                    isChanged = true;
                }

                if (vert.colorData[2] != vertRGB[2]) {
                    vert.colorData[2] = vertRGB[2];
                    isChanged = true;
                }
            }
        }
    }

    // "zbuffer_write" attribute.
    if (truePBRData.contains("zbuffer_write") && truePBRData["zbuffer_write"].is_boolean()) {
        const auto newZBufferWrite = truePBRData["zbuffer_write"].get<bool>();
        isChanged |= PGNIFUtil::configureShaderFlag(nifShaderBSLSP, nifly::SLSF2_ZBUFFER_WRITE, newZBufferWrite);
    }

    // "specular_level" attribute.
    if (truePBRData.contains("specular_level") && truePBRData["specular_level"].is_number()) {
        const auto newSpecularLevel = truePBRData["specular_level"].get<float>();
        if (nifShader->GetGlossiness() != newSpecularLevel) {
            nifShader->SetGlossiness(newSpecularLevel);
            isChanged = true;
        }
    }

    // "subsurface_color" attribute.
    if (truePBRData.contains("subsurface_color") && truePBRData["subsurface_color"].is_array()
        && truePBRData["subsurface_color"].size() >= 3 && truePBRData["subsurface_color"][0].is_number()
        && truePBRData["subsurface_color"][1].is_number() && truePBRData["subsurface_color"][2].is_number()) {
        const auto newSpecularColor = nifly::Vector3(truePBRData["subsurface_color"][0].get<float>(),
                                                     truePBRData["subsurface_color"][1].get<float>(),
                                                     truePBRData["subsurface_color"][2].get<float>());
        if (nifShader->GetSpecularColor() != newSpecularColor) {
            nifShader->SetSpecularColor(newSpecularColor);
            isChanged = true;
        }
    }

    // "roughness_scale" attribute.
    if (truePBRData.contains("roughness_scale") && truePBRData["roughness_scale"].is_number()) {
        const auto newRoughnessScale = truePBRData["roughness_scale"].get<float>();
        if (nifShader->GetSpecularStrength() != newRoughnessScale) {
            nifShader->SetSpecularStrength(newRoughnessScale);
            isChanged = true;
        }
    }

    // "subsurface_opacity" attribute.
    if (truePBRData.contains("subsurface_opacity") && truePBRData["subsurface_opacity"].is_number()) {
        const auto newSubsurfaceOpacity = truePBRData["subsurface_opacity"].get<float>();
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newSubsurfaceOpacity);
    }

    // "displacement_scale" attribute.
    if (truePBRData.contains("displacement_scale") && truePBRData["displacement_scale"].is_number()) {
        const auto newDisplacementScale = truePBRData["displacement_scale"].get<float>();
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->rimlightPower, newDisplacementScale);
    }

    // "emmissive_scale" attribute.
    if (truePBRData.contains("emissive_scale") && truePBRData["emissive_scale"].is_number()) {
        const auto newEmissiveScale = truePBRData["emissive_scale"].get<float>();
        if (nifShader->GetEmissiveMultiple() != newEmissiveScale) {
            nifShader->SetEmissiveMultiple(newEmissiveScale);
            isChanged = true;
        }
    }

    // "emmissive_color" attribute.
    if (truePBRData.contains("emissive_color") && truePBRData["emissive_color"].is_array()
        && truePBRData["emissive_color"].size() >= 4 && truePBRData["emissive_color"][0].is_number()
        && truePBRData["emissive_color"][1].is_number() && truePBRData["emissive_color"][2].is_number()
        && truePBRData["emissive_color"][3].is_number()) {
        const auto newEmissiveColor = nifly::Color4(truePBRData["emissive_color"][0].get<float>(),
                                                    truePBRData["emissive_color"][1].get<float>(),
                                                    truePBRData["emissive_color"][2].get<float>(),
                                                    truePBRData["emissive_color"][3].get<float>());
        if (nifShader->GetEmissiveColor() != newEmissiveColor) {
            nifShader->SetEmissiveColor(newEmissiveColor);
            isChanged = true;
        }
    }

    // "uv_scale" attribute.
    if (truePBRData.contains("uv_scale") && truePBRData["uv_scale"].is_number()) {
        const auto newUVScale
            = nifly::Vector2(truePBRData["uv_scale"].get<float>(), truePBRData["uv_scale"].get<float>());
        isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // Enable PBR on the shape (always on, the legacy "pbr" JSON field is ignored).
    isChanged |= enableTruePBROnShape(nifShader, nifShaderBSLSP, truePBRData, matchedPath, newSlots);

    return isChanged;
}

void PatcherMeshShaderTruePBR::applyOnePatchSlots(PGTypes::TextureSet& slots,
                                                  const nlohmann::json& truePBRData,
                                                  const std::wstring& matchedPath)
{
    // "lock_diffuse" attribute.
    if (!(truePBRData.contains("lock_diffuse") && truePBRData["lock_diffuse"].is_boolean()
          && truePBRData["lock_diffuse"].get<bool>())) {
        const auto newDiffuse = matchedPath + L".dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::Diffuse)] = newDiffuse;
    }

    // "lock_normal" attribute.
    if (!(truePBRData.contains("lock_normal") && truePBRData["lock_normal"].is_boolean()
          && truePBRData["lock_normal"].get<bool>())) {
        const auto newNormal = matchedPath + L"_n.dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::Normal)] = newNormal;
    }

    // "emissive" attribute.
    if (truePBRData.contains("emissive") && truePBRData["emissive"].is_boolean()
        && !(truePBRData.contains("lock_emissive") && truePBRData["lock_emissive"].is_boolean()
             && truePBRData["lock_emissive"].get<bool>())) {
        std::wstring newGlow;
        if (truePBRData["emissive"].get<bool>())
            newGlow = matchedPath + L"_g.dds";

        slots[static_cast<size_t>(PGEnums::TextureSlots::Glow)] = newGlow;
    }

    // "parallax" attribute.
    if (truePBRData.contains("parallax") && truePBRData["parallax"].is_boolean()
        && !(truePBRData.contains("lock_parallax") && truePBRData["lock_parallax"].is_boolean()
             && truePBRData["lock_parallax"].get<bool>())) {
        std::wstring newParallax;
        if (truePBRData["parallax"].get<bool>())
            newParallax = matchedPath + L"_p.dds";

        slots[static_cast<size_t>(PGEnums::TextureSlots::Parallax)] = newParallax;
    }

    // "cubemap" attribute.
    if (truePBRData.contains("cubemap") && truePBRData["cubemap"].is_string()
        && !(truePBRData.contains("lock_cubemap") && truePBRData["lock_cubemap"].is_boolean()
             && truePBRData["lock_cubemap"].get<bool>())) {
        const auto newCubemap = StringUtil::utf8toUTF16(truePBRData["cubemap"].get<std::string>());
        slots[static_cast<size_t>(PGEnums::TextureSlots::Cubemap)] = newCubemap;
    } else {
        slots[static_cast<size_t>(PGEnums::TextureSlots::Cubemap)] = L"";
    }

    // "lock_rmaos" attribute.
    if (!(truePBRData.contains("lock_rmaos") && truePBRData["lock_rmaos"].is_boolean()
          && truePBRData["lock_rmaos"].get<bool>())) {
        const auto newRMAOS = matchedPath + L"_rmaos.dds";
        slots[static_cast<size_t>(PGEnums::TextureSlots::EnvMask)] = newRMAOS;
    }

    // "lock_cnr" attribute.
    if (!(truePBRData.contains("lock_cnr") && truePBRData["lock_cnr"].is_boolean()
          && truePBRData["lock_cnr"].get<bool>())) {
        // "coat_normal" attribute.
        std::wstring newCNR;
        if (truePBRData.contains("coat_normal") && truePBRData["coat_normal"].is_boolean()
            && truePBRData["coat_normal"].get<bool>()) {
            newCNR = matchedPath + L"_cnr.dds";
        }

        // Fuzz texture slot.
        if (truePBRData.contains("fuzz") && truePBRData["fuzz"].is_object() && truePBRData["fuzz"].contains("texture")
            && truePBRData["fuzz"]["texture"].is_boolean() && truePBRData["fuzz"]["texture"].get<bool>()) {
            newCNR = matchedPath + L"_f.dds";
        }

        slots[static_cast<size_t>(PGEnums::TextureSlots::MultiLayer)] = newCNR;
    }

    // "lock_subsurface" attribute.
    if (!(truePBRData.contains("lock_subsurface") && truePBRData["lock_subsurface"].is_boolean()
          && truePBRData["lock_subsurface"].get<bool>())) {
        // "subsurface_foliage" attribute.
        std::wstring newSubsurface;
        if ((truePBRData.contains("subsurface_foliage") && truePBRData["subsurface_foliage"].is_boolean()
             && truePBRData["subsurface_foliage"].get<bool>())
            || (truePBRData.contains("subsurface") && truePBRData["subsurface"].is_boolean()
                && truePBRData["subsurface"].get<bool>())
            || (truePBRData.contains("coat_diffuse") && truePBRData["coat_diffuse"].is_boolean()
                && truePBRData["coat_diffuse"].get<bool>())) {
            newSubsurface = matchedPath + L"_s.dds";
        }

        slots[static_cast<size_t>(PGEnums::TextureSlots::Backlight)] = newSubsurface;
    }

    // "SlotX" attributes.
    for (int i = 0; i < numTextureSlots - 1; i++) {
        std::string slotName("slot");
        slotName += std::to_string(i + 1);

        if (truePBRData.contains(slotName) && truePBRData[slotName].is_string()) {
            std::string newSlot = truePBRData[slotName].get<std::string>();
            StringUtil::toLowerASCIIFastInPlace(newSlot);

            // Prepend "textures\\" if it's not already there.
            if (!newSlot.empty() && !newSlot.starts_with("textures\\"))
                newSlot.insert(0, "textures\\");

            slots.at(i) = StringUtil::utf8toUTF16(newSlot);
        }
    }
}

bool PatcherMeshShaderTruePBR::enableTruePBROnShape(nifly::NiShader* nifShader,
                                                    nifly::BSLightingShaderProperty* nifShaderBSLSP,
                                                    nlohmann::json& truePBRData,
                                                    const std::wstring& matchedPath,
                                                    PGTypes::TextureSet& newSlots)
{
    bool isChanged = false;

    applyOnePatchSlots(newSlots, truePBRData, matchedPath);

    // "emissive" attribute.
    if (truePBRData.contains("emissive") && truePBRData["emissive"].is_boolean()) {
        isChanged |= PGNIFUtil::configureShaderFlag(
            nifShaderBSLSP, nifly::SLSF1_EXTERNAL_EMITTANCE, truePBRData["emissive"].get<bool>());
    }

    // Revert to default NIFShader type, remove flags used in other types.
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_ENVIRONMENT_MAPPING);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_HAIR_SOFT_LIGHTING);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_PARALLAX);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_GLOW_MAP);
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_FACEGEN_DETAIL_MAP);

    // Enable PBR flag.
    isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_UNUSED01);

    // Disable any unused flags that might cause issues.
    isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF1_EYE_ENVIRONMENT_MAPPING);

    // "subsurface" attribute.
    if (truePBRData.contains("subsurface") && truePBRData["subsurface"].is_boolean()) {
        isChanged |= PGNIFUtil::configureShaderFlag(
            nifShaderBSLSP, nifly::SLSF2_RIM_LIGHTING, truePBRData["subsurface"].get<bool>());
    }

    // "hair" attribute.
    if (truePBRData.contains("hair") && truePBRData["hair"].is_boolean() && truePBRData["hair"].get<bool>())
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING);

    // "multilayer" attribute.
    bool enableMultiLayer = false;
    if (truePBRData.contains("multilayer") && truePBRData["multilayer"].is_boolean()
        && truePBRData["multilayer"].get<bool>()) {
        enableMultiLayer = true;

        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);

        // "coat_color" attribute.
        if (truePBRData.contains("coat_color") && truePBRData["coat_color"].size() >= 3
            && truePBRData["coat_color"][0].is_number() && truePBRData["coat_color"][1].is_number()
            && truePBRData["coat_color"][2].is_number()) {
            const auto newCoatColor = nifly::Vector3(truePBRData["coat_color"][0].get<float>(),
                                                     truePBRData["coat_color"][1].get<float>(),
                                                     truePBRData["coat_color"][2].get<float>());
            if (nifShader->GetSpecularColor() != newCoatColor) {
                nifShader->SetSpecularColor(newCoatColor);
                isChanged = true;
            }
        }

        // "coat_specular_level" attribute.
        if (truePBRData.contains("coat_specular_level") && truePBRData["coat_specular_level"].is_number()) {
            const auto newCoatSpecularLevel = truePBRData["coat_specular_level"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, newCoatSpecularLevel);
        }

        // "coat_roughness" attribute.
        if (truePBRData.contains("coat_roughness") && truePBRData["coat_roughness"].is_number()) {
            const auto newCoatRoughness = truePBRData["coat_roughness"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, newCoatRoughness);
        }

        // "coat_strength" attribute.
        if (truePBRData.contains("coat_strength") && truePBRData["coat_strength"].is_number()) {
            const auto newCoatStrength = truePBRData["coat_strength"].get<float>();
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newCoatStrength);
        }

        // "coat_diffuse" attribute.
        if (truePBRData.contains("coat_diffuse") && truePBRData["coat_diffuse"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_EFFECT_LIGHTING, truePBRData["coat_diffuse"].get<bool>());
        }

        // "coat_parallax" attribute.
        if (truePBRData.contains("coat_parallax") && truePBRData["coat_parallax"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING, truePBRData["coat_parallax"].get<bool>());
        }

        // "coat_normal" attribute.
        if (truePBRData.contains("coat_normal") && truePBRData["coat_normal"].is_boolean()) {
            isChanged |= PGNIFUtil::configureShaderFlag(
                nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING, truePBRData["coat_normal"].get<bool>());
        }

        // "inner_uv_scale" attribute.
        if (truePBRData.contains("inner_uv_scale") && truePBRData["inner_uv_scale"].is_number()) {
            const auto newInnerUVScale = nifly::Vector2(truePBRData["inner_uv_scale"].get<float>(),
                                                        truePBRData["inner_uv_scale"].get<float>());
            isChanged |= PGNIFUtil::setShaderVec2(nifShaderBSLSP->parallaxInnerLayerTextureScale, newInnerUVScale);
        }
    } else if (truePBRData.contains("glint") && truePBRData["glint"].is_object()) {
        // Glint is enabled.
        const auto& glintParams = truePBRData["glint"];

        // Set shader type to MLP.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        // Enable Glint with FitSlope flag.
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_FIT_SLOPE);

        // Glint parameters.
        if (glintParams.contains("screen_space_scale") && glintParams["screen_space_scale"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness,
                                                   glintParams["screen_space_scale"]);
        }

        if (glintParams.contains("log_microfacet_density") && glintParams["log_microfacet_density"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale,
                                                   glintParams["log_microfacet_density"]);
        }

        if (glintParams.contains("microfacet_roughness") && glintParams["microfacet_roughness"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.u,
                                                   glintParams["microfacet_roughness"]);
        }

        if (glintParams.contains("density_randomization") && glintParams["density_randomization"].is_number()) {
            isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.v,
                                                   glintParams["density_randomization"]);
        }
    } else if (truePBRData.contains("fuzz") && truePBRData["fuzz"].is_object()) {
        // Fuzz is enabled.
        const auto& fuzzParams = truePBRData["fuzz"];

        // Set shader type to MLP.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_MULTILAYERPARALLAX);
        // Enable Fuzz with soft lighting flag.
        isChanged |= PGNIFUtil::setShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING);

        // Get color.
        auto fuzzColor = std::vector<float> { 0, 0, 0 };
        if (fuzzParams.contains("color") && fuzzParams["color"].is_array() && fuzzParams["color"].size() == 3
            && fuzzParams["color"][0].is_number() && fuzzParams["color"][1].is_number()
            && fuzzParams["color"][2].is_number()) {
            fuzzColor = fuzzParams["color"].get<std::vector<float>>();
        }

        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, fuzzColor[0]);
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, fuzzColor[1]);
        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.u, fuzzColor[2]);

        // Get weight.
        float fuzzWeight = 1;
        if (fuzzParams.contains("weight") && fuzzParams["weight"].is_number())
            fuzzWeight = fuzzParams["weight"].get<float>();

        isChanged |= PGNIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.v, fuzzWeight);
    } else {
        // Revert to default NIFShader type.
        isChanged |= PGNIFUtil::setShaderType(nifShader, nifly::BSLSP_DEFAULT);
    }

    if (!enableMultiLayer) {
        // Clear multilayer flags.
        isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_MULTI_LAYER_PARALLAX);

        if (!(truePBRData.contains("hair") && truePBRData["hair"].is_boolean() && truePBRData["hair"].get<bool>()))
            isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_BACK_LIGHTING);

        if (!truePBRData.contains("fuzz") || !truePBRData["fuzz"].is_object())
            isChanged |= PGNIFUtil::clearShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING);
    }

    return isChanged;
}

//
// Helpers.
//

nifly::Vector2 PatcherMeshShaderTruePBR::abs2(nifly::Vector2 v) { return { abs(v.u), abs(v.v) }; }

nifly::Vector2 PatcherMeshShaderTruePBR::autoUVScale(const std::vector<nifly::Vector2>* uvs,
                                                     const std::vector<nifly::Vector3>* verts,
                                                     std::vector<nifly::Triangle>& tris)
{
    nifly::Vector2 scale;
    for (const nifly::Triangle& t : tris) {
        const auto v1 = (*verts)[t.p1];
        const auto v2 = (*verts)[t.p2];
        const auto v3 = (*verts)[t.p3];
        const auto uv1 = (*uvs)[t.p1];
        const auto uv2 = (*uvs)[t.p2];
        const auto uv3 = (*uvs)[t.p3];

        const auto s = (abs2(uv2 - uv1) + abs2(uv3 - uv1)) / ((v2 - v1).length() + (v3 - v1).length());
        scale += nifly::Vector2(1 / s.u, 1 / s.v);
    }

    scale *= 10.0 / 4.0;
    scale /= static_cast<float>(tris.size());
    scale.u = std::min(scale.u, scale.v);
    scale.v = std::min(scale.u, scale.v);

    return scale;
}
