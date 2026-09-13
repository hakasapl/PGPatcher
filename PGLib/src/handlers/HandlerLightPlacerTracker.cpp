#include "handlers/HandlerLightPlacerTracker.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "util/FileUtil.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Statics.
std::vector<std::unique_ptr<HandlerLightPlacerTracker::LPJSON>> HandlerLightPlacerTracker::s_lightPlacerJSONs;
std::unordered_map<std::filesystem::path, std::vector<std::pair<HandlerLightPlacerTracker::LPJSON*, nlohmann::json*>>>
    HandlerLightPlacerTracker::s_lightPlacerJSONMap;

void HandlerLightPlacerTracker::init(const std::vector<std::filesystem::path>& lpJSONs)
{
    static auto* const pgd = PGGlobals::pgd();

    // Clear stale model->JSON pointer mappings from prior runs.
    s_lightPlacerJSONMap.clear();

    // Clear and reserve space for new LPJSONs.
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONs.reserve(lpJSONs.size());

    for (const auto& jsonPath : lpJSONs) {
        // Load JSON data.
        nlohmann::json jsonData;
        if (!FileUtil::getJSON(pgd->looseFileFullPath(jsonPath), jsonData)) {
            // Unable to load.
            continue;
        }

        // Add to s_lightPlacerJSONMutexMap.
        s_lightPlacerJSONs.emplace_back(std::make_unique<LPJSON>(jsonPath, std::move(jsonData)));
        auto* const lpJsonPtr = s_lightPlacerJSONs.back().get();

        // Loop through json to create nif map.
        for (const auto& block : lpJsonPtr->jsonData.items()) {
            if (!block.value().is_object() || !block.value().contains("models"))
                continue; // skip if not a valid light placer block

            auto& models = block.value()["models"];
            if (!models.is_array())
                continue; // skip if models is not an array

            for (const auto& model : models) {
                if (!model.is_string())
                    continue; // skip if model is not a string

                const std::filesystem::path modelPath = model.get<std::string>();
                s_lightPlacerJSONMap[modelPath].emplace_back(lpJsonPtr, &models);
            }
        }
    }
}

void HandlerLightPlacerTracker::handleNIFCreated(const std::filesystem::path& baseNIFPath,
                                                 const std::filesystem::path& createdNIFPath)
{
    if (baseNIFPath == createdNIFPath) {
        // Not a duplicate nif, no reason to continue.
        return;
    }

    // Remove "meshes" from from the first part of both paths.
    const auto baseNIFPathLP = PGPlugin::pluginPathFromDataPath(baseNIFPath);
    const auto createdNIFPathLP = PGPlugin::pluginPathFromDataPath(createdNIFPath);

    // Check if s_lightPlacerJSONMap contains the baseNIFPath.
    const auto it = s_lightPlacerJSONMap.find(baseNIFPathLP);
    if (it == s_lightPlacerJSONMap.end()) {
        // No light placer JSONs for this nif.
        return;
    }

    // Get the vector of LPJSON pointers and models.
    const auto& lpJSONs = it->second;

    // Loop through each LPJSON and update the models.
    for (const auto& [lpJsonPtr, models] : lpJSONs) {
        // Lock mutex for modifying json.
        const std::scoped_lock lock(lpJsonPtr->jsonMutex);

        // Add to models if not already present (models is already a json array).
        const auto createdNIFPathStr = StringUtil::utf16toUTF8(createdNIFPathLP.wstring());
        if (!StringUtil::checkIfStringInJSONArray(*models, createdNIFPathStr)) {
            models->push_back(createdNIFPathStr);
            lpJsonPtr->isChanged = true; // mark as changed
        }
    }
}

void HandlerLightPlacerTracker::finalize()
{
    // Get PGD object.
    static const auto* const pgd = PGGlobals::pgd();
    static const auto generatedDir = pgd->generatedPath();

    // Loop through each LPJSON and save if changed.
    for (const auto& lpJsonPtr : s_lightPlacerJSONs) {
        if (lpJsonPtr->isChanged) {
            const auto outputPath = generatedDir / lpJsonPtr->jsonPath;
            // Ensure the directory exists.
            if (!std::filesystem::exists(outputPath.parent_path()))
                std::filesystem::create_directories(outputPath.parent_path());

            FileUtil::saveJSON(outputPath, lpJsonPtr->jsonData, true);
        }
    }

    // Clear the static members.
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONMap.clear();
}
