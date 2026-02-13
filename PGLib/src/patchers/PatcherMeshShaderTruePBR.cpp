#include "patchers/PatcherMeshShaderTruePBR.hpp"

#include "ParallaxGenPlugin.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

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
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

PatcherMeshShaderTruePBR::PatcherMeshShaderTruePBR(filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshShader(std::move(nifPath), nif, "TruePBR")
{
}

auto PatcherMeshShaderTruePBR::getTruePBRConfigs() -> map<size_t, nlohmann::json>&
{
    static map<size_t, nlohmann::json> truePBRConfigs = {};
    return truePBRConfigs;
}

auto PatcherMeshShaderTruePBR::getPathLookupJSONs() -> map<size_t, nlohmann::json>&
{
    static map<size_t, nlohmann::json> pathLookupJSONs = {};
    return pathLookupJSONs;
}

auto PatcherMeshShaderTruePBR::getTruePBRDiffuseInverse() -> map<wstring, vector<size_t>>&
{
    static map<wstring, vector<size_t>> truePBRDiffuseInverse = {};
    return truePBRDiffuseInverse;
}

auto PatcherMeshShaderTruePBR::getTruePBRNormalInverse() -> map<wstring, vector<size_t>>&
{
    static map<wstring, vector<size_t>> truePBRNormalInverse = {};
    return truePBRNormalInverse;
}

auto PatcherMeshShaderTruePBR::getPathLookupCache() -> unordered_map<tuple<wstring, wstring>, bool, TupleStrHash>&
{
    static unordered_map<tuple<wstring, wstring>, bool, TupleStrHash> pathLookupCache = {};
    return pathLookupCache;
}

auto PatcherMeshShaderTruePBR::getPathLookupCacheMutex() -> std::mutex&
{
    static std::mutex cacheMutex;
    return cacheMutex;
}

auto PatcherMeshShaderTruePBR::getTruePBRConfigFilenameFields() -> vector<string>
{
    static const vector<string> pgConfigFilenameFields = { "match_normal", "match_diffuse", "rename" };
    return pgConfigFilenameFields;
}

// Statics
void PatcherMeshShaderTruePBR::loadStatics(const std::vector<std::filesystem::path>& pbrJSONs)
{
    size_t configOrder = 0;
    for (const auto& config : pbrJSONs) {
        // check if Config is valid
        auto configFileBytes = getPGD()->getFile(config);
        std::string configFileStr;
        std::ranges::transform(
            configFileBytes, std::back_inserter(configFileStr), [](std::byte b) { return static_cast<char>(b); });

        try {
            nlohmann::json j = nlohmann::json::parse(configFileStr);
            nlohmann::json jDefaults;
            nlohmann::json jEntries;

            // check if j is a json object
            if (j.is_object()) {
                if (!j.contains("default") || !j.contains("entries")) {
                    continue;
                }

                jDefaults = j["default"];
                jEntries = j["entries"];
            } else {
                jDefaults = nlohmann::json::object();
                jEntries = j;
            }

            // loop through each Element
            for (auto& element : jEntries) {
                // merge defaults with element
                for (const auto& [key, value] : jDefaults.items()) {
                    if (!element.contains(key)) {
                        element[key] = value;
                    }
                }

                // Preprocessing steps here
                if (element.contains("texture")) {
                    element["match_diffuse"] = element["texture"];
                }

                element["json"] = ParallaxGenUtil::utf16toUTF8(config.wstring());

                // loop through filename Fields
                for (const auto& field : getTruePBRConfigFilenameFields()) {
                    if (element.contains(field) && !boost::istarts_with(element[field].get<string>(), "\\")) {
                        element[field] = element[field].get<string>().insert(0, 1, '\\');
                    }
                }

                Logger::trace(
                    L"TruePBR Config {} Loaded: {}", configOrder, ParallaxGenUtil::utf8toUTF16(element.dump()));
                getTruePBRConfigs()[configOrder++] = element;
            }
        } catch (nlohmann::json::parse_error& e) {
            Logger::error(L"Unable to parse TruePBR Config file {}: {}", config.wstring(),
                ParallaxGenUtil::utf8toUTF16(e.what()));
            continue;
        }
    }

    Logger::info(L"Found {} TruePBR entries", getTruePBRConfigs().size());

    // Create helper vectors
    for (const auto& config : getTruePBRConfigs()) {
        // "match_normal" attribute
        if (config.second.contains("match_normal")) {
            auto revNormal = ParallaxGenUtil::utf8toUTF16(config.second["match_normal"].get<string>());
            revNormal = NIFUtil::getTexBase(revNormal);
            std::ranges::reverse(revNormal);

            getTruePBRNormalInverse()[ParallaxGenUtil::toLowerASCIIFast(revNormal)].push_back(config.first);
            continue;
        }

        // "match_diffuse" attribute
        if (config.second.contains("match_diffuse")) {
            auto revDiffuse = ParallaxGenUtil::utf8toUTF16(config.second["match_diffuse"].get<string>());
            revDiffuse = NIFUtil::getTexBase(revDiffuse);
            std::ranges::reverse(revDiffuse);

            getTruePBRDiffuseInverse()[ParallaxGenUtil::toLowerASCIIFast(revDiffuse)].push_back(config.first);
        }

        // "path_contains" attribute
        if (config.second.contains("path_contains")) {
            getPathLookupJSONs()[config.first] = config.second;
        }
    }
}

auto PatcherMeshShaderTruePBR::getFactory() -> PatcherMeshShader::PatcherMeshShaderFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshShader> {
        return make_unique<PatcherMeshShaderTruePBR>(nifPath, nif);
    };
}

auto PatcherMeshShaderTruePBR::getShaderType() -> NIFUtil::ShapeShader { return NIFUtil::ShapeShader::TRUEPBR; }

auto PatcherMeshShaderTruePBR::canApply([[maybe_unused]] nifly::NiShape& nifShape, [[maybe_unused]] bool singlepassMATO,
    const ParallaxGenPlugin::ModelRecordType& modelRecordType) -> bool
{
    if (modelRecordType == ParallaxGenPlugin::ModelRecordType::GRASS) {
        // grass is not supported
        return false;
    }

    auto* const nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    return !NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF1_FACEGEN_RGB_TINT);
}

auto PatcherMeshShaderTruePBR::shouldApply(nifly::NiShape& nifShape, std::vector<PatcherMatch>& matches) -> bool
{
    // Prep
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    matches.clear();

    // Find Old Slots
    auto oldSlots = getTextureSet(getNIFPath(), *getNIF(), nifShape);

    shouldApply(oldSlots, matches);

    if (NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01)) {
        // Check if RMAOS exists
        const auto& rmaosPath = oldSlots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)];
        if (!rmaosPath.empty() && getPGD()->isFile(rmaosPath)) {
            PatcherMatch match;
            match.matchedPath = getNIFPath().wstring();
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

auto PatcherMeshShaderTruePBR::shouldApply(const NIFUtil::TextureSet& oldSlots, std::vector<PatcherMatch>& matches)
    -> bool
{
    // get search prefixes
    auto searchPrefixes = NIFUtil::getSearchPrefixes(oldSlots, false);
    // only normal map gets _n part removed to match properly
    searchPrefixes[1] = NIFUtil::getTexBase(oldSlots[1], NIFUtil::TextureSlots::NORMAL);

    // Remove "pbr" part if starts with "textures\\pbr" for each search prefix
    static constexpr size_t TEXTURE_PBR_STR_LENGTH = 13; // length of "textures\pbr\"
    for (auto& prefix : searchPrefixes) {
        if (ParallaxGenUtil::toLowerASCIIFast(prefix).starts_with(L"textures\\pbr\\")) {
            prefix.replace(0, TEXTURE_PBR_STR_LENGTH, L"textures\\");
        }
    }

    map<size_t, tuple<nlohmann::json, wstring>> truePBRData;
    // "match_normal" attribute: Binary search for normal map
    getSlotMatch(truePBRData, searchPrefixes[1], getTruePBRNormalInverse(), getNIFPath().wstring());

    // "match_diffuse" attribute: Binary search for diffuse map
    getSlotMatch(truePBRData, searchPrefixes[0], getTruePBRDiffuseInverse(), getNIFPath().wstring());

    // "path_contains" attribute: Linear search for path_contains
    getPathContainsMatch(truePBRData, searchPrefixes[0], getNIFPath().wstring());

    // Split data into individual JSONs
    unordered_map<wstring, map<size_t, tuple<nlohmann::json, wstring>>> truePBROutputData;
    unordered_map<wstring, unordered_set<NIFUtil::TextureSlots>> truePBRMatchedFrom;
    for (const auto& [sequence, data] : truePBRData) {
        // get current JSON
        auto matchedPath = ParallaxGenUtil::utf8toUTF16(get<0>(data)["json"].get<string>());

        // Add to output
        if (!truePBROutputData.contains(matchedPath)) {
            // If MatchedPath doesn't exist, insert it with an empty map and then add Sequence, Data
            truePBROutputData.emplace(matchedPath, map<size_t, tuple<nlohmann::json, wstring>> {});
        }
        truePBROutputData[matchedPath][sequence] = data;

        if (get<0>(data).contains("match_normal")) {
            truePBRMatchedFrom[matchedPath].insert(NIFUtil::TextureSlots::NORMAL);
        } else {
            truePBRMatchedFrom[matchedPath].insert(NIFUtil::TextureSlots::DIFFUSE);
        }
    }

    // Convert output to vectors
    for (auto& [json, jsonData] : truePBROutputData) {
        PatcherMatch match;
        match.matchedPath = json;
        match.matchedFrom = truePBRMatchedFrom[json];
        match.extraData = make_shared<decltype(jsonData)>(jsonData);

        // loop through json data
        bool deleteShape = false;
        for (const auto& [sequence, data] : jsonData) {
            if (get<0>(data).contains("delete") && get<0>(data)["delete"].get<bool>()) {
                // marked for deletion, skip slot checks
                deleteShape = true;
                break;
            }
        }

        // check paths
        bool valid = true;

        if (!deleteShape) {
            NIFUtil::TextureSet newSlots = oldSlots;
            applyPatchSlots(newSlots, match);
            for (size_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
                if (!newSlots.at(i).empty() && !getPGD()->isFile(newSlots.at(i))) {
                    // Slot does not exist
                    if (s_printNonExistentPaths) {
                        Logger::warn(
                            L"Texture \"{}\" does not exist from PBR json \"{}\" when patching mesh \"{}\" (Skipping)",
                            newSlots.at(i), match.matchedPath, getNIFPath().wstring());
                    }

                    // only invalidate if checkpaths is false
                    if (s_checkPaths) {
                        valid = false;
                    }
                }
            }
        }

        if (!valid) {
            continue;
        }

        matches.push_back(match);
    }

    // Sort matches by ExtraData key minimum value (this preserves order of JSONs to be 0 having priority if mod order
    // does not exist)
    std::ranges::sort(matches, [](const PatcherMatch& a, const PatcherMatch& b) {
        return get<0>(*(static_pointer_cast<map<size_t, tuple<nlohmann::json, wstring>>>(a.extraData)->begin()))
            > get<0>(*(static_pointer_cast<map<size_t, tuple<nlohmann::json, wstring>>>(b.extraData)->begin()));
    });

    // Check for pre-patch case
    if (truePBRData.empty()) {
        const auto& rmaosPath = oldSlots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)];
        // if not start with PBR add it for the check
        if (getPGD()->getTextureType(rmaosPath) == NIFUtil::TextureType::RMAOS) {
            // found RMAOS without json
            PatcherMatch match;
            match.matchedPath = rmaosPath;
            match.extraData = nullptr;
            matches.insert(matches.begin(), match);
        }
    }

    return !matches.empty();
}

void PatcherMeshShaderTruePBR::getSlotMatch(map<size_t, tuple<nlohmann::json, wstring>>& truePBRData,
    const wstring& texName, const map<wstring, vector<size_t>>& lookup, const wstring& nifPath)
{
    // binary search for map
    auto mapReverse = ParallaxGenUtil::toLowerASCIIFast(texName);
    std::ranges::reverse(mapReverse);
    auto it = lookup.lower_bound(mapReverse);

    // get the first element of the reverse path
    auto reverseFile = mapReverse;
    auto pos = reverseFile.find_first_of(L'\\');
    if (pos != wstring::npos) {
        reverseFile = reverseFile.substr(0, pos);
    }

    // Check if match is 1 back
    if (it != lookup.begin() && boost::starts_with(prev(it)->first, reverseFile)) {
        it = prev(it);
    } else if (it != lookup.end() && boost::starts_with(it->first, reverseFile)) {
        // Check if match is current iterator, just continue here
    } else {
        // No match found
        return;
    }

    auto beginIt = it;
    while (beginIt != lookup.begin()) {
        if (boost::starts_with(prev(beginIt)->first, reverseFile)) {
            beginIt = prev(beginIt);
        } else {
            break;
        }
    }

    // Initialize CFG set
    set<size_t> cfgs;

    // create vector of all matches based on beginIt and It
    while (beginIt != next(it)) {
        if (!boost::starts_with(mapReverse, beginIt->first)) {
            // not a valid match
            beginIt = next(beginIt);
            continue;
        }

        cfgs.insert(beginIt->second.begin(), beginIt->second.end());
        beginIt = next(beginIt);
    }

    if (cfgs.empty()) {
        return;
    }

    // Loop through all matches
    for (const auto& cfg : cfgs) {
        insertTruePBRData(truePBRData, texName, cfg, nifPath);
    }
}

void PatcherMeshShaderTruePBR::getPathContainsMatch(
    std::map<size_t, std::tuple<nlohmann::json, std::wstring>>& truePBRData, const std::wstring& diffuse,
    const wstring& nifPath)
{
    // "patch_contains" attribute: Linear search for path_contains
    auto& cache = getPathLookupCache();
    auto& cacheMutex = getPathLookupCacheMutex();

    // Check for path_contains only if no name match because it's a O(n) operation
    for (const auto& config : getPathLookupJSONs()) {
        // Check if in cache
        auto cacheKey = make_tuple(ParallaxGenUtil::utf8toUTF16(config.second["path_contains"].get<string>()), diffuse);

        bool pathMatch = false;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            if (cache.find(cacheKey) == cache.end()) {
                // Not in cache, update it
                cache[cacheKey] = boost::icontains(diffuse, get<0>(cacheKey));
            }
            pathMatch = cache[cacheKey];
        }

        if (pathMatch) {
            insertTruePBRData(truePBRData, diffuse, config.first, nifPath);
        }
    }
}

auto PatcherMeshShaderTruePBR::insertTruePBRData(
    std::map<size_t, std::tuple<nlohmann::json, std::wstring>>& truePBRData, const wstring& texName, size_t cfg,
    const wstring& nifPath) -> void
{
    const auto curCfg = getTruePBRConfigs()[cfg];

    // Check if we should skip this due to nif filter (this is expsenive, so we do it last)
    if (curCfg.contains("nif_filter") && !boost::icontains(nifPath, curCfg["nif_filter"].get<string>())) {
        return;
    }

    // Find and check prefix value
    // Add the PBR part to the texture path
    auto texPath = texName;
    const auto texPathLower = ParallaxGenUtil::toLowerASCIIFast(texPath);
    if (texPathLower.starts_with(L"textures\\") && !texPathLower.starts_with(L"textures\\pbr\\")) {
        texPath.replace(0, TEXTURE_STR_LENGTH, L"textures\\pbr\\");
    }

    // Get PBR path, which is the path without the matched field
    auto matchedField = curCfg.contains("match_normal") ? NIFUtil::getTexBase(curCfg["match_normal"].get<string>())
                                                        : NIFUtil::getTexBase(curCfg["match_diffuse"].get<string>());
    texPath.erase(texPath.length() - matchedField.length(), matchedField.length());

    // "rename" attribute
    if (curCfg.contains("rename")) {
        auto renameField = curCfg["rename"].get<string>();
        if (!ParallaxGenUtil::asciiFastIEquals(renameField, matchedField)) {
            matchedField = ParallaxGenUtil::utf8toUTF16(renameField);
        }
    }

    // Check if named_field is a directory
    wstring matchedPath = ParallaxGenUtil::toLowerASCIIFast(texPath + matchedField);
    const bool enableTruePBR = (!curCfg.contains("pbr") || curCfg["pbr"]) && !matchedPath.empty();
    if (!enableTruePBR) {
        matchedPath = L"";
    }

    truePBRData.insert({ cfg, { curCfg, matchedPath } });
}

void PatcherMeshShaderTruePBR::applyPatch(
    NIFUtil::TextureSet& slots, nifly::NiShape& nifShape, const PatcherMatch& match)
{
    if (match.extraData == nullptr) {
        // no extra data, so this is a pre-patched mesh, do nothing
        return;
    }

    auto extraData = static_pointer_cast<map<size_t, tuple<nlohmann::json, wstring>>>(match.extraData);
    for (const auto& [Sequence, Data] : *extraData) {
        // apply one patch
        auto truePBRData = get<0>(Data);
        auto matchedPath = get<1>(Data);
        applyOnePatch(&nifShape, truePBRData, matchedPath, slots);
    }
}

void PatcherMeshShaderTruePBR::applyPatchSlots(NIFUtil::TextureSet& slots, const PatcherMatch& match)
{
    if (match.extraData == nullptr) {
        return;
    }

    auto extraData = static_pointer_cast<map<size_t, tuple<nlohmann::json, wstring>>>(match.extraData);
    for (const auto& [Sequence, Data] : *extraData) {
        auto truePBRData = get<0>(Data);
        auto matchedPath = get<1>(Data);
        applyOnePatchSlots(slots, truePBRData, matchedPath);
    }
}

void PatcherMeshShaderTruePBR::applyShader(nifly::NiShape& nifShape)
{
    // Contrary to the other patchers, this one is generic and is not called normally other than setting for plugins,
    // later material swaps in CS are used

    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    // Set default PBR shader type
    NIFUtil::setShaderType(nifShader, BSLSP_DEFAULT);
    NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);

    // Clear unused flags
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_HAIR_SOFT_LIGHTING);
}

void PatcherMeshShaderTruePBR::loadOptions(unordered_map<string, string>& optionsStr)
{
    for (const auto& [option, value] : optionsStr) {
        if (option == "no_path_check") {
            s_checkPaths = false;
        }

        if (option == "print_nonexistent_paths") {
            s_printNonExistentPaths = true;
        }
    }
}

void PatcherMeshShaderTruePBR::loadOptions(const bool& checkPaths, const bool& printNonExistentPaths)
{
    s_checkPaths = checkPaths;
    s_printNonExistentPaths = printNonExistentPaths;
}

auto PatcherMeshShaderTruePBR::applyOnePatch(NiShape* nifShape, nlohmann::json& truePBRData,
    const std::wstring& matchedPath, NIFUtil::TextureSet& newSlots) -> bool
{
    bool changed = false;

    // Prep
    auto* nifShader = getNIF()->GetShader(nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    const bool enableTruePBR = !matchedPath.empty();
    const bool enableEnvMapping = truePBRData.contains("env_mapping") && truePBRData["env_mapping"] && !enableTruePBR;

    // "delete" attribute
    if (truePBRData.contains("delete") && truePBRData["delete"]) {
        getNIF()->DeleteShape(nifShape);
        changed = true;
        return changed;
    }

    // "smooth_angle" attribute
    if (truePBRData.contains("smooth_angle")) {
        getNIF()->CalcNormalsForShape(nifShape, true, true, truePBRData["smooth_angle"]);
        getNIF()->CalcTangentsForShape(nifShape);
        changed = true;
    }

    // "auto_uv" attribute
    if (truePBRData.contains("auto_uv")) {
        vector<Triangle> tris;
        nifShape->GetTriangles(tris);
        auto newUVScale = autoUVScale(getNIF()->GetUvsForShape(nifShape), getNIF()->GetVertsForShape(nifShape), tris)
            / truePBRData["auto_uv"];
        changed |= NIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // "vertex_colors" attribute
    if (truePBRData.contains("vertex_colors")) {
        auto newVertexColors = truePBRData["vertex_colors"].get<bool>();
        if (nifShape->HasVertexColors() != newVertexColors) {
            nifShape->SetVertexColors(newVertexColors);
            changed = true;
        }

        if (nifShader->HasVertexColors() != newVertexColors) {
            nifShader->SetVertexColors(newVertexColors);
            changed = true;
        }
    }

    // "vertex_color_lum_mult" and "vertex_color_sat_mult" attribute
    if (nifShape->HasVertexColors()
        && (truePBRData.contains("vertex_color_lum_mult") || truePBRData.contains("vertex_color_sat_mult"))) {
        vector<BSVertexData>* vertData = nullptr;
        if (dynamic_cast<nifly::BSTriShape*>(nifShape) != nullptr) {
            vertData = &dynamic_cast<nifly::BSTriShape*>(nifShape)->vertData;
        } else if (dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape) != nullptr) {
            vertData = &dynamic_cast<nifly::BSMeshLODTriShape*>(nifShape)->vertData;
        }

        if (vertData != nullptr) {
            for (auto& vert : *vertData) {
                // Convert to HSL and multiply luminance then convert back
                boost::gil::rgb8_pixel_t vertRGB(vert.colorData[0], vert.colorData[1], vert.colorData[2]);
                boost::gil::hsl32f_pixel_t vertHSL;
                boost::gil::color_convert(vertRGB, vertHSL);

                float newLVal = vertHSL[2];
                if (truePBRData.contains("vertex_color_lum_mult")) {
                    const auto newVertexColorMult = truePBRData["vertex_color_lum_mult"].get<float>();
                    newLVal = 1 - (1 - vertHSL[2]) * newVertexColorMult;
                }

                float newSVal = vertHSL[1];
                if (truePBRData.contains("vertex_color_sat_mult")) {
                    const auto newVertexColorMult = truePBRData["vertex_color_sat_mult"].get<float>();
                    newSVal = vertHSL[1] * newVertexColorMult;
                }

                vertHSL[1] = clamp(newSVal, 0.0F, 1.0F);
                vertHSL[2] = clamp(newLVal, 0.0F, 1.0F);
                boost::gil::color_convert(vertHSL, vertRGB);

                if (vert.colorData[0] != vertRGB[0]) {
                    vert.colorData[0] = vertRGB[0];
                    changed = true;
                }

                if (vert.colorData[1] != vertRGB[1]) {
                    vert.colorData[1] = vertRGB[1];
                    changed = true;
                }

                if (vert.colorData[2] != vertRGB[2]) {
                    vert.colorData[2] = vertRGB[2];
                    changed = true;
                }
            }
        }
    }

    // "zbuffer_write" attribute
    if (truePBRData.contains("zbuffer_write")) {
        auto newZBufferWrite = truePBRData["zbuffer_write"].get<bool>();
        changed |= NIFUtil::configureShaderFlag(nifShaderBSLSP, SLSF2_ZBUFFER_WRITE, newZBufferWrite);
    }

    // "specular_level" attribute
    if (truePBRData.contains("specular_level")) {
        auto newSpecularLevel = truePBRData["specular_level"].get<float>();
        if (nifShader->GetGlossiness() != newSpecularLevel) {
            nifShader->SetGlossiness(newSpecularLevel);
            changed = true;
        }
    }

    // "subsurface_color" attribute
    if (truePBRData.contains("subsurface_color") && truePBRData["subsurface_color"].size() >= 3) {
        auto newSpecularColor = Vector3(truePBRData["subsurface_color"][0].get<float>(),
            truePBRData["subsurface_color"][1].get<float>(), truePBRData["subsurface_color"][2].get<float>());
        if (nifShader->GetSpecularColor() != newSpecularColor) {
            nifShader->SetSpecularColor(newSpecularColor);
            changed = true;
        }
    }

    // "roughness_scale" attribute
    if (truePBRData.contains("roughness_scale")) {
        auto newRoughnessScale = truePBRData["roughness_scale"].get<float>();
        if (nifShader->GetSpecularStrength() != newRoughnessScale) {
            nifShader->SetSpecularStrength(newRoughnessScale);
            changed = true;
        }
    }

    // "subsurface_opacity" attribute
    if (truePBRData.contains("subsurface_opacity")) {
        auto newSubsurfaceOpacity = truePBRData["subsurface_opacity"].get<float>();
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newSubsurfaceOpacity);
    }

    // "displacement_scale" attribute
    if (truePBRData.contains("displacement_scale")) {
        auto newDisplacementScale = truePBRData["displacement_scale"].get<float>();
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->rimlightPower, newDisplacementScale);
    }

    // "EnvMapping" attribute
    if (enableEnvMapping) {
        changed |= NIFUtil::setShaderType(nifShader, BSLSP_ENVMAP);
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING);
    }

    // "EnvMap_scale" attribute
    if (truePBRData.contains("env_map_scale") && enableEnvMapping) {
        auto newEnvMapScale = truePBRData["env_map_scale"].get<float>();
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->environmentMapScale, newEnvMapScale);
    }

    // "EnvMap_scale_mult" attribute
    if (truePBRData.contains("env_map_scale_mult") && enableEnvMapping) {
        nifShaderBSLSP->environmentMapScale *= truePBRData["env_map_scale_mult"].get<float>();
        changed = true;
    }

    // "emmissive_scale" attribute
    if (truePBRData.contains("emissive_scale")) {
        auto newEmissiveScale = truePBRData["emissive_scale"].get<float>();
        if (nifShader->GetEmissiveMultiple() != newEmissiveScale) {
            nifShader->SetEmissiveMultiple(newEmissiveScale);
            changed = true;
        }
    }

    // "emmissive_color" attribute
    if (truePBRData.contains("emissive_color") && truePBRData["emissive_color"].size() >= 4) {
        auto newEmissiveColor
            = Color4(truePBRData["emissive_color"][0].get<float>(), truePBRData["emissive_color"][1].get<float>(),
                truePBRData["emissive_color"][2].get<float>(), truePBRData["emissive_color"][3].get<float>());
        if (nifShader->GetEmissiveColor() != newEmissiveColor) {
            nifShader->SetEmissiveColor(newEmissiveColor);
            changed = true;
        }
    }

    // "uv_scale" attribute
    if (truePBRData.contains("uv_scale")) {
        auto newUVScale = Vector2(truePBRData["uv_scale"].get<float>(), truePBRData["uv_scale"].get<float>());
        changed |= NIFUtil::setShaderVec2(nifShaderBSLSP->uvScale, newUVScale);
    }

    // "pbr" attribute
    if (enableTruePBR) {
        // no pbr, we can return here
        changed |= enableTruePBROnShape(nifShape, nifShader, nifShaderBSLSP, truePBRData, matchedPath, newSlots);
    }

    return changed;
}

void PatcherMeshShaderTruePBR::applyOnePatchSlots(
    NIFUtil::TextureSet& slots, const nlohmann::json& truePBRData, const std::wstring& matchedPath)
{
    if (matchedPath.empty()) {
        return;
    }

    // "lock_diffuse" attribute
    if (!flag(truePBRData, "lock_diffuse")) {
        auto newDiffuse = matchedPath + L".dds";
        slots[static_cast<size_t>(NIFUtil::TextureSlots::DIFFUSE)] = newDiffuse;
    }

    // "lock_normal" attribute
    if (!flag(truePBRData, "lock_normal")) {
        auto newNormal = matchedPath + L"_n.dds";
        slots[static_cast<size_t>(NIFUtil::TextureSlots::NORMAL)] = newNormal;
    }

    // "emissive" attribute
    if (truePBRData.contains("emissive") && !flag(truePBRData, "lock_emissive")) {
        wstring newGlow;
        if (truePBRData["emissive"].get<bool>()) {
            newGlow = matchedPath + L"_g.dds";
        }

        slots[static_cast<size_t>(NIFUtil::TextureSlots::GLOW)] = newGlow;
    }

    // "parallax" attribute
    if (truePBRData.contains("parallax") && !flag(truePBRData, "lock_parallax")) {
        wstring newParallax;
        if (truePBRData["parallax"].get<bool>()) {
            newParallax = matchedPath + L"_p.dds";
        }

        slots[static_cast<size_t>(NIFUtil::TextureSlots::PARALLAX)] = newParallax;
    }

    // "cubemap" attribute
    if (truePBRData.contains("cubemap") && !flag(truePBRData, "lock_cubemap")) {
        auto newCubemap = ParallaxGenUtil::utf8toUTF16(truePBRData["cubemap"].get<string>());
        slots[static_cast<size_t>(NIFUtil::TextureSlots::CUBEMAP)] = newCubemap;
    } else {
        slots[static_cast<size_t>(NIFUtil::TextureSlots::CUBEMAP)] = L"";
    }

    // "lock_rmaos" attribute
    if (!flag(truePBRData, "lock_rmaos")) {
        auto newRMAOS = matchedPath + L"_rmaos.dds";
        slots[static_cast<size_t>(NIFUtil::TextureSlots::ENVMASK)] = newRMAOS;
    }

    // "lock_cnr" attribute
    if (!flag(truePBRData, "lock_cnr")) {
        // "coat_normal" attribute
        wstring newCNR;
        if (truePBRData.contains("coat_normal") && truePBRData["coat_normal"].get<bool>()) {
            newCNR = matchedPath + L"_cnr.dds";
        }

        // Fuzz texture slot
        if (truePBRData.contains("fuzz") && flag(truePBRData["fuzz"], "texture")) {
            newCNR = matchedPath + L"_f.dds";
        }

        slots[static_cast<size_t>(NIFUtil::TextureSlots::MULTILAYER)] = newCNR;
    }

    // "lock_subsurface" attribute
    if (!flag(truePBRData, "lock_subsurface")) {
        // "subsurface_foliage" attribute
        wstring newSubsurface;
        if ((truePBRData.contains("subsurface_foliage") && truePBRData["subsurface_foliage"].get<bool>())
            || (truePBRData.contains("subsurface") && truePBRData["subsurface"].get<bool>())
            || (truePBRData.contains("coat_diffuse") && truePBRData["coat_diffuse"].get<bool>())) {
            newSubsurface = matchedPath + L"_s.dds";
        }

        slots[static_cast<size_t>(NIFUtil::TextureSlots::BACKLIGHT)] = newSubsurface;
    }

    // "SlotX" attributes
    for (int i = 0; i < NUM_TEXTURE_SLOTS - 1; i++) {
        std::string slotName("slot");
        slotName += std::to_string(i + 1);

        if (truePBRData.contains(slotName)) {
            std::string newSlot = truePBRData[slotName].get<std::string>();
            ParallaxGenUtil::toLowerASCIIFastInPlace(newSlot);

            // Prepend "textures\\" if it's not already there
            if (!newSlot.empty() && !newSlot.starts_with("textures\\")) {
                newSlot.insert(0, "textures\\");
            }

            slots.at(i) = ParallaxGenUtil::utf8toUTF16(newSlot);
        }
    }
}

auto PatcherMeshShaderTruePBR::enableTruePBROnShape(NiShape* nifShape, NiShader* nifShader,
    BSLightingShaderProperty* nifShaderBSLSP, nlohmann::json& truePBRData, const wstring& matchedPath,
    NIFUtil::TextureSet& newSlots) -> bool
{
    bool changed = false;

    applyOnePatchSlots(newSlots, truePBRData, matchedPath);

    // "emissive" attribute
    if (truePBRData.contains("emissive")) {
        changed |= NIFUtil::configureShaderFlag(
            nifShaderBSLSP, SLSF1_EXTERNAL_EMITTANCE, truePBRData["emissive"].get<bool>());
    }

    // revert to default NIFShader type, remove flags used in other types
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_ENVIRONMENT_MAPPING);
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_HAIR_SOFT_LIGHTING);
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_PARALLAX);
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_GLOW_MAP);

    // Enable PBR flag
    changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_UNUSED01);

    // Disable any unused flags that might cause issues
    changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF1_EYE_ENVIRONMENT_MAPPING);

    // "subsurface" attribute
    if (truePBRData.contains("subsurface")) {
        changed
            |= NIFUtil::configureShaderFlag(nifShaderBSLSP, SLSF2_RIM_LIGHTING, truePBRData["subsurface"].get<bool>());
    }

    // "hair" attribute
    if (flag(truePBRData, "hair")) {
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING);
    }

    // "multilayer" attribute
    bool enableMultiLayer = false;
    if (truePBRData.contains("multilayer") && truePBRData["multilayer"]) {
        enableMultiLayer = true;

        changed |= NIFUtil::setShaderType(nifShader, BSLSP_MULTILAYERPARALLAX);
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);

        // "coat_color" attribute
        if (truePBRData.contains("coat_color") && truePBRData["coat_color"].size() >= 3) {
            auto newCoatColor = Vector3(truePBRData["coat_color"][0].get<float>(),
                truePBRData["coat_color"][1].get<float>(), truePBRData["coat_color"][2].get<float>());
            if (nifShader->GetSpecularColor() != newCoatColor) {
                nifShader->SetSpecularColor(newCoatColor);
                changed = true;
            }
        }

        // "coat_specular_level" attribute
        if (truePBRData.contains("coat_specular_level")) {
            auto newCoatSpecularLevel = truePBRData["coat_specular_level"].get<float>();
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, newCoatSpecularLevel);
        }

        // "coat_roughness" attribute
        if (truePBRData.contains("coat_roughness")) {
            auto newCoatRoughness = truePBRData["coat_roughness"].get<float>();
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, newCoatRoughness);
        }

        // "coat_strength" attribute
        if (truePBRData.contains("coat_strength")) {
            auto newCoatStrength = truePBRData["coat_strength"].get<float>();
            changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, newCoatStrength);
        }

        // "coat_diffuse" attribute
        if (truePBRData.contains("coat_diffuse")) {
            changed |= NIFUtil::configureShaderFlag(
                nifShaderBSLSP, SLSF2_EFFECT_LIGHTING, truePBRData["coat_diffuse"].get<bool>());
        }

        // "coat_parallax" attribute
        if (truePBRData.contains("coat_parallax")) {
            changed |= NIFUtil::configureShaderFlag(
                nifShaderBSLSP, SLSF2_SOFT_LIGHTING, truePBRData["coat_parallax"].get<bool>());
        }

        // "coat_normal" attribute
        if (truePBRData.contains("coat_normal")) {
            changed |= NIFUtil::configureShaderFlag(
                nifShaderBSLSP, SLSF2_BACK_LIGHTING, truePBRData["coat_normal"].get<bool>());
        }

        // "inner_uv_scale" attribute
        if (truePBRData.contains("inner_uv_scale")) {
            auto newInnerUVScale
                = Vector2(truePBRData["inner_uv_scale"].get<float>(), truePBRData["inner_uv_scale"].get<float>());
            changed |= NIFUtil::setShaderVec2(nifShaderBSLSP->parallaxInnerLayerTextureScale, newInnerUVScale);
        }
    } else if (truePBRData.contains("glint")) {
        // glint is enabled
        const auto& glintParams = truePBRData["glint"];

        // Set shader type to MLP
        changed |= NIFUtil::setShaderType(nifShader, BSLSP_MULTILAYERPARALLAX);
        // Enable Glint with FitSlope flag
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_FIT_SLOPE);

        // Glint parameters
        if (glintParams.contains("screen_space_scale")) {
            changed |= NIFUtil::setShaderFloat(
                nifShaderBSLSP->parallaxInnerLayerThickness, glintParams["screen_space_scale"]);
        }

        if (glintParams.contains("log_microfacet_density")) {
            changed |= NIFUtil::setShaderFloat(
                nifShaderBSLSP->parallaxRefractionScale, glintParams["log_microfacet_density"]);
        }

        if (glintParams.contains("microfacet_roughness")) {
            changed |= NIFUtil::setShaderFloat(
                nifShaderBSLSP->parallaxInnerLayerTextureScale.u, glintParams["microfacet_roughness"]);
        }

        if (glintParams.contains("density_randomization")) {
            changed |= NIFUtil::setShaderFloat(
                nifShaderBSLSP->parallaxInnerLayerTextureScale.v, glintParams["density_randomization"]);
        }
    } else if (truePBRData.contains("fuzz")) {
        // fuzz is enabled
        const auto& fuzzParams = truePBRData["fuzz"];

        // Set shader type to MLP
        changed |= NIFUtil::setShaderType(nifShader, BSLSP_MULTILAYERPARALLAX);
        // Enable Fuzz with soft lighting flag
        changed |= NIFUtil::setShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING);

        // get color
        auto fuzzColor = vector<float> { 0.0F, 0.0F, 0.0F };
        if (fuzzParams.contains("color")) {
            fuzzColor = fuzzParams["color"].get<vector<float>>();
        }

        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerThickness, fuzzColor[0]);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxRefractionScale, fuzzColor[1]);
        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.u, fuzzColor[2]);

        // get weight
        auto fuzzWeight = 1.0F;
        if (fuzzParams.contains("weight")) {
            fuzzWeight = fuzzParams["weight"].get<float>();
        }

        changed |= NIFUtil::setShaderFloat(nifShaderBSLSP->parallaxInnerLayerTextureScale.v, fuzzWeight);
    } else {
        // Revert to default NIFShader type
        changed |= NIFUtil::setShaderType(nifShader, BSLSP_DEFAULT);
    }

    if (!enableMultiLayer) {
        // Clear multilayer flags
        changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_MULTI_LAYER_PARALLAX);

        if (!flag(truePBRData, "hair")) {
            changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_BACK_LIGHTING);
        }

        if (!truePBRData.contains("fuzz")) {
            changed |= NIFUtil::clearShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING);
        }
    }

    return changed;
}

//
// Helpers
//

auto PatcherMeshShaderTruePBR::abs2(Vector2 v) -> Vector2 { return { abs(v.u), abs(v.v) }; }

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
auto PatcherMeshShaderTruePBR::autoUVScale(
    const vector<Vector2>* uvs, const vector<Vector3>* verts, vector<Triangle>& tris) -> Vector2
{
    Vector2 scale;
    for (const Triangle& t : tris) {
        auto v1 = (*verts)[t.p1];
        auto v2 = (*verts)[t.p2];
        auto v3 = (*verts)[t.p3];
        auto uv1 = (*uvs)[t.p1];
        auto uv2 = (*uvs)[t.p2];
        auto uv3 = (*uvs)[t.p3];

        auto s = (abs2(uv2 - uv1) + abs2(uv3 - uv1)) / ((v2 - v1).length() + (v3 - v1).length());
        scale += Vector2(1.0F / s.u, 1.0F / s.v);
    }

    scale *= 10.0 / 4.0;
    scale /= static_cast<float>(tris.size());
    scale.u = min(scale.u, scale.v);
    scale.v = min(scale.u, scale.v);

    return scale;
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

auto PatcherMeshShaderTruePBR::flag(const nlohmann::json& json, const char* key) -> bool
{
    return json.contains(key) && json[key];
}
