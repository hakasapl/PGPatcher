#include "handlers/HandlerLightPlacerTracker.hpp"
#include "PGGlobals.hpp"
#include "util/ParallaxGenUtil.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json_fwd.hpp>

using namespace std;

// statics
vector<unique_ptr<HandlerLightPlacerTracker::LPJSON>> HandlerLightPlacerTracker::s_lightPlacerJSONs;
unordered_map<filesystem::path, vector<pair<HandlerLightPlacerTracker::LPJSON*, nlohmann::json*>>>
    HandlerLightPlacerTracker::s_lightPlacerJSONMap;

void HandlerLightPlacerTracker::init(const vector<filesystem::path>& lpJSONs)
{
    static auto* const pgd = PGGlobals::getPGD();

    // Clear and reserve space for new LPJSONs
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONs.reserve(lpJSONs.size());

    for (const auto& jsonPath : lpJSONs) {
        // Load JSON data
        nlohmann::json jsonData;
        if (!ParallaxGenUtil::getJSON(pgd->getLooseFileFullPath(jsonPath), jsonData)) {
            // unable to load
            continue;
        }

        // Add to s_lightPlacerJSONMutexMap
        s_lightPlacerJSONs.emplace_back(make_unique<LPJSON>(jsonPath, std::move(jsonData)));
        auto* const lpJsonPtr = s_lightPlacerJSONs.back().get();

        // loop through json to create nif map
        for (const auto& block : lpJsonPtr->jsonData.items()) {
            if (!block.value().is_object() || !block.value().contains("models")) {
                continue; // skip if not a valid light placer block
            }

            auto& models = block.value()["models"];
            if (!models.is_array()) {
                continue; // skip if models is not an array
            }

            for (const auto& model : models) {
                if (!model.is_string()) {
                    continue; // skip if model is not a string
                }

                const filesystem::path modelPath = model.get<string>();
                s_lightPlacerJSONMap[modelPath].emplace_back(lpJsonPtr, &models);
            }
        }
    }
}

void HandlerLightPlacerTracker::handleNIFCreated(
    const filesystem::path& baseNIFPath, const filesystem::path& createdNIFPath)
{
    if (baseNIFPath == createdNIFPath) {
        // Not a duplicate nif, no reason to continue
        return;
    }

    // Remove "meshes" from from the first part of both paths
    const auto baseNIFPathLP = ParallaxGenUtil::getPluginPathFromDataPath(baseNIFPath);
    const auto createdNIFPathLP = ParallaxGenUtil::getPluginPathFromDataPath(createdNIFPath);

    // check if s_lightPlacerJSONMap contains the baseNIFPath
    auto it = s_lightPlacerJSONMap.find(baseNIFPathLP);
    if (it == s_lightPlacerJSONMap.end()) {
        // No light placer JSONs for this nif
        return;
    }

    // Get the vector of LPJSON pointers and models
    auto& lpJSONs = it->second;

    // Loop through each LPJSON and update the models
    for (const auto& [lpJsonPtr, models] : lpJSONs) {
        // lock mutex for modifying json
        const lock_guard<mutex> lock(lpJsonPtr->jsonMutex);

        // Add to models if not already present (models is already a json array)
        const auto createdNIFPathStr = ParallaxGenUtil::utf16toUTF8(createdNIFPathLP.wstring());
        if (!ParallaxGenUtil::checkIfStringInJSONArray(*models, createdNIFPathStr)) {
            models->push_back(createdNIFPathStr);
            lpJsonPtr->changed = true; // mark as changed
        }
    }
}

void HandlerLightPlacerTracker::finalize()
{
    // get PGD object
    static auto* const pgd = PGGlobals::getPGD();
    static const auto generatedDir = pgd->getGeneratedPath();

    // Loop through each LPJSON and save if changed
    for (const auto& lpJsonPtr : s_lightPlacerJSONs) {
        if (lpJsonPtr->changed) {
            const auto outputPath = generatedDir / lpJsonPtr->jsonPath;
            // Ensure the directory exists
            if (!filesystem::exists(outputPath.parent_path())) {
                filesystem::create_directories(outputPath.parent_path());
            }

            ParallaxGenUtil::saveJSON(outputPath, lpJsonPtr->jsonData, true);
        }
    }

    // Clear the static members
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONMap.clear();
}
