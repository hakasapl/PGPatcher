#pragma once

#include <filesystem>
#include <mutex>

#include <nlohmann/json.hpp>

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
    static void init(const std::vector<std::filesystem::path>& lpJSONs);

    /**
     * @brief Handles the creation of a NIF file and updates the associated light placer JSONs. THIS IS THE ONLY METHOD
     * THAT IS THREAD SAFE
     *
     * @param baseNIFPath base NIF path
     * @param createdNIFPath created NIF path (which may be the same)
     */
    static void handleNIFCreated(const std::filesystem::path& baseNIFPath, const std::filesystem::path& createdNIFPath);
    static void finalize();
};
