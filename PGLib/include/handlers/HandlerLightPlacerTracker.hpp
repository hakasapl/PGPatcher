#pragma once

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

class HandlerLightPlacerTracker {
private:
    struct LPJSON {
        std::filesystem::path jsonPath;
        std::mutex jsonMutex;
        nlohmann::json jsonData;
        bool changed = false;

        LPJSON(std::filesystem::path path, nlohmann::json data = {})
            : jsonPath(std::move(path))
            , jsonData(std::move(data))
        {
        }
    };

    static std::vector<std::unique_ptr<LPJSON>> s_lightPlacerJSONs;
    static std::unordered_map<std::filesystem::path, std::vector<std::pair<LPJSON*, nlohmann::json*>>>
        s_lightPlacerJSONMap;

public:
    /**
     * @brief Initializes the light placer tracker with the provided JSON files. NOT THREAD SAFE
     *
     * @param lpJSONs A vector of paths to the light placer JSON files to be tracked.
     */
    static void init(const std::vector<std::filesystem::path>& lpJSONs);

    /**
     * @brief Handles the creation of a NIF file and updates the associated light placer JSONs. THIS IS THE ONLY METHOD
     * THAT IS THREAD SAFE
     *
     * @param baseNIFPath base NIF path
     * @param createdNIFPath created NIF path (which may be the same)
     */
    static void handleNIFCreated(const std::filesystem::path& baseNIFPath, const std::filesystem::path& createdNIFPath);

    /**
     * @brief Finalizes the light placer tracker, releasing any resources and cleaning up. NOT THREAD SAFE
     */
    static void finalize();
};
