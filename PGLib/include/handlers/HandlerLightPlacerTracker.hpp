#pragma once

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

/**
 * @brief Keeps the Light Placer JSONs of the load order in step with the output meshes.
 *
 * A Light Placer JSON lists the models its lights attach to. When PGPatcher writes a duplicate of a mesh, the
 * duplicate is added to every entry that lists the original, and the changed JSONs are written to the output folder.
 */
class HandlerLightPlacerTracker {
private:
    struct LPJSON {
        std::filesystem::path jsonPath;
        std::mutex jsonMutex;
        nlohmann::json jsonData;
        bool isChanged = false;

        explicit LPJSON(std::filesystem::path path,
                        nlohmann::json data = { })
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
     * @brief Parses a Light Placer config the way Light Placer itself does (glaze with its default options): a byte
     * order mark is rejected, whatever follows the root value is ignored, and the root has to be an array.
     *
     * @param fullPath Path of the JSON file.
     * @param[out] out Parsed JSON.
     * @return true if Light Placer would load the file.
     */
    static bool parseLightPlacerJSON(const std::filesystem::path& fullPath,
                                     nlohmann::json& out);

    /**
     * @brief The key a config's model entry is matched by: the lower case path, so a mesh path compares equal to it
     * whatever the case and the separators of the entry (Light Placer compares model strings case insensitively).
     *
     * @param model A model entry of a config, UTF-8.
     */
    static std::filesystem::path modelKey(const std::string& model);

    /**
     * @brief Reads the given Light Placer JSONs from the data folder (loose files, through PGGlobals::pgd()) and
     * indexes their model entries. A JSON Light Placer itself would reject is skipped with a warning. NOT THREAD SAFE
     *
     * @param lpJSONs Lower case relative paths of the Light Placer JSON files to track.
     */
    static void init(const std::vector<std::filesystem::path>& lpJSONs);

    /**
     * @brief Adds an output mesh to every Light Placer entry that lists the mesh it was created from. Nothing happens
     * when both paths are equal. THIS IS THE ONLY METHOD THAT IS THREAD SAFE
     *
     * @param baseNIFPath lower case relative path of the original mesh
     * @param createdNIFPath relative path of the output mesh (which may be the same)
     */
    static void handleNIFCreated(const std::filesystem::path& baseNIFPath,
                                 const std::filesystem::path& createdNIFPath);

    /**
     * @brief Writes every changed JSON to the output folder and forgets all tracked JSONs. NOT THREAD SAFE
     */
    static void finalize();
};
