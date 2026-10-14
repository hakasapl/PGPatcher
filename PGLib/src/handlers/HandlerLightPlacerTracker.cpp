#include "handlers/HandlerLightPlacerTracker.hpp"

#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json_fwd.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

/**
 * @brief Finds where the root value of a JSON text ends.
 *
 * @param text JSON text.
 * @return Offset one past the bracket that closes a root array or object. The text size is returned for a scalar
 * root or an unterminated root so that the parser reports the problem.
 */
size_t rootValueEnd(const std::string& text)
{
    size_t pos = text.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos || (text[pos] != '[' && text[pos] != '{'))
        return text.size();

    int depth = 0;
    bool isInString = false;
    for (; pos < text.size(); pos++) {
        const char c = text[pos];
        if (isInString) {
            if (c == '\\')
                pos++; // an escaped character cannot end the string
            else if (c == '"')
                isInString = false;
            continue;
        }

        if (c == '"') {
            isInString = true;
        } else if (c == '[' || c == '{') {
            depth++;
        } else if (c == ']' || c == '}') {
            depth--;
            if (!depth)
                return pos + 1;
        }
    }

    return text.size();
}

} // namespace

bool HandlerLightPlacerTracker::parseLightPlacerJSON(const std::filesystem::path& fullPath,
                                                     nlohmann::json& out)
{
    const auto bytes = FileUtil::fileBytes(fullPath);
    std::string text;
    text.reserve(bytes.size());
    std::ranges::transform(bytes, std::back_inserter(text), [](std::byte byte) { return static_cast<char>(byte); });

    static constexpr std::string_view byteOrderMark = "\xEF\xBB\xBF";
    if (text.starts_with(byteOrderMark))
        return false;

    out = nlohmann::json::parse(std::string_view(text).substr(0, rootValueEnd(text)), nullptr, false);
    return out.is_array();
}

// Statics.
std::vector<std::unique_ptr<HandlerLightPlacerTracker::LPJSON>> HandlerLightPlacerTracker::s_lightPlacerJSONs;
std::unordered_map<std::filesystem::path, std::vector<std::pair<HandlerLightPlacerTracker::LPJSON*, nlohmann::json*>>>
    HandlerLightPlacerTracker::s_lightPlacerJSONMap;

std::filesystem::path HandlerLightPlacerTracker::modelKey(const std::string& model)
{
    // Light Placer matches model paths regardless of case and PGPatcher's mesh paths are lower case, so the key is
    // lower case too; a path compares its elements, so the separators of the entry do not matter.
    return StringUtil::toLowerASCIIFast(StringUtil::utf8toUTF16(model));
}

void HandlerLightPlacerTracker::init(const std::vector<std::filesystem::path>& lpJSONs)
{
    auto* const pgd = PGGlobals::pgd();

    // Clear stale model->JSON pointer mappings from prior runs.
    s_lightPlacerJSONMap.clear();

    // Clear and reserve space for new LPJSONs.
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONs.reserve(lpJSONs.size());

    for (const auto& jsonPath : lpJSONs) {
        // Load JSON data.
        nlohmann::json jsonData;
        if (!parseLightPlacerJSON(pgd->looseFileFullPath(jsonPath), jsonData)) {
            Logger::warn(L"Light Placer JSON {} is invalid and was skipped", jsonPath.wstring());
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

                s_lightPlacerJSONMap[modelKey(model.get<std::string>())].emplace_back(lpJsonPtr, &models);
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

    // Remove "meshes" from the first part of both paths. The lookup key is lower case like the keys built in init().
    const std::filesystem::path baseNIFPathLP
        = StringUtil::toLowerASCIIFast(PGPlugin::pluginPathFromDataPath(baseNIFPath).wstring());
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
    // Loop through each LPJSON and save if changed. The output directory is only needed when something is written.
    for (const auto& lpJsonPtr : s_lightPlacerJSONs) {
        if (!lpJsonPtr->isChanged)
            continue;

        const auto outputPath = PGGlobals::pgd()->generatedPath() / lpJsonPtr->jsonPath;
        // Ensure the directory exists.
        if (!std::filesystem::exists(outputPath.parent_path()))
            std::filesystem::create_directories(outputPath.parent_path());

        FileUtil::saveJSON(outputPath, lpJsonPtr->jsonData, true);
    }

    // Clear the static members.
    s_lightPlacerJSONs.clear();
    s_lightPlacerJSONMap.clear();
}
