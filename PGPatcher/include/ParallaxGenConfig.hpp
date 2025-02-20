#pragma once

#include <filesystem>
#include <mutex>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <unordered_map>

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "NIFUtil.hpp"

/**
 * @class ParallaxGenConfig
 * @brief Class responsible for ParallaxGen runtime configuration and JSON file interaction
 */
class ParallaxGenConfig {
public:
    /**
     * @struct PGParams
     * @brief Struct that holds all the user-configurable parameters for ParallaxGen
     */
    struct PGParams {
        // Game
        struct Game {
            std::filesystem::path dir;
            BethesdaGame::GameType type = BethesdaGame::GameType::SKYRIM_SE;

            auto operator==(const Game& other) const -> bool { return dir == other.dir && type == other.type; }
        } Game;

        // Mod Manager
        struct ModManager {
            ModManagerDirectory::ModManagerType type = ModManagerDirectory::ModManagerType::NONE;
            std::filesystem::path mo2InstanceDir;
            std::wstring mo2Profile = L"Default";
            bool mo2UseOrder = true;

            auto operator==(const ModManager& other) const -> bool
            {
                return type == other.type && mo2InstanceDir == other.mo2InstanceDir && mo2Profile == other.mo2Profile
                    && mo2UseOrder == other.mo2UseOrder;
            }
        } ModManager;

        // Output
        struct Output {
            std::filesystem::path dir;
            bool zip = true;

            auto operator==(const Output& other) const -> bool { return dir == other.dir && zip == other.zip; }
        } Output;

        // Advanced
        bool advanced = false;

        // Processing
        struct Processing {
            bool multithread = true;
            bool highMem = false;
            bool bsa = true;
            bool pluginPatching = true;
            bool pluginESMify = false;
            bool mapFromMeshes = true;
            bool diagnostics = false;

            auto operator==(const Processing& other) const -> bool
            {
                return multithread == other.multithread && highMem == other.highMem && bsa == other.bsa
                    && pluginPatching == other.pluginPatching && mapFromMeshes == other.mapFromMeshes
                    && diagnostics == other.diagnostics;
            }
        } Processing;

        // Pre-Patchers
        struct PrePatcher {
            bool disableMLP = false;
            bool fixMeshLighting = false;

            auto operator==(const PrePatcher& other) const -> bool
            {
                return disableMLP == other.disableMLP && fixMeshLighting == other.fixMeshLighting;
            }
        } PrePatcher;

        // Shader Patchers
        struct ShaderPatcher {
            bool parallax = true;
            bool complexMaterial = true;

            struct ShaderPatcherComplexMaterial {
                std::vector<std::wstring> listsDyncubemapBlocklist;

                auto operator==(const ShaderPatcherComplexMaterial& other) const -> bool
                {
                    return listsDyncubemapBlocklist == other.listsDyncubemapBlocklist;
                }
            } ShaderPatcherComplexMaterial;

            bool truePBR = true;
            struct ShaderPatcherTruePBR {
                bool checkPaths = true;
                bool printNonExistentPaths = false;

                auto operator==(const ShaderPatcherTruePBR& other) const -> bool
                {
                    return checkPaths == other.checkPaths && printNonExistentPaths == other.printNonExistentPaths;
                }
            } ShaderPatcherTruePBR;

            auto operator==(const ShaderPatcher& other) const -> bool
            {
                return parallax == other.parallax && complexMaterial == other.complexMaterial
                    && truePBR == other.truePBR && ShaderPatcherComplexMaterial == other.ShaderPatcherComplexMaterial
                    && ShaderPatcherTruePBR == other.ShaderPatcherTruePBR;
            }
        } ShaderPatcher;

        // Shader Transforms
        struct ShaderTransforms {
            bool parallaxToCM = false;

            auto operator==(const ShaderTransforms& other) const -> bool { return parallaxToCM == other.parallaxToCM; }
        } ShaderTransforms;

        // Post-Patchers
        struct PostPatcher {
            bool optimizeMeshes = false;
            bool fixSSS = false;

            auto operator==(const PostPatcher& other) const -> bool
            {
                return optimizeMeshes == other.optimizeMeshes && fixSSS == other.fixSSS;
            }
        } PostPatcher;

        // Lists
        struct MeshRules {
            std::vector<std::wstring> allowList;
            std::vector<std::wstring> blockList;

            auto operator==(const MeshRules& other) const -> bool
            {
                return allowList == other.allowList && blockList == other.blockList;
            }
        } MeshRules;

        struct TextureRules {
            std::vector<std::wstring> vanillaBSAList;
            std::vector<std::pair<std::wstring, NIFUtil::TextureType>> textureMaps;

            auto operator==(const TextureRules& other) const -> bool
            {
                return vanillaBSAList == other.vanillaBSAList && textureMaps == other.textureMaps;
            }
        } TextureRules;

        [[nodiscard]] auto getString() const -> std::wstring;

        auto operator==(const PGParams& other) const -> bool
        {
            return Game == other.Game && ModManager == other.ModManager && Output == other.Output
                && Processing == other.Processing && PrePatcher == other.PrePatcher
                && ShaderPatcher == other.ShaderPatcher && ShaderTransforms == other.ShaderTransforms
                && PostPatcher == other.PostPatcher && MeshRules == other.MeshRules
                && TextureRules == other.TextureRules && advanced == other.advanced;
        }

        auto operator!=(const PGParams& other) const -> bool { return !(*this == other); }
    };

private:
    static std::filesystem::path s_exePath; /** Stores the ExePath of ParallaxGen.exe */

    PGParams m_params; /** Stores the configured parameters */

    std::mutex m_modOrderMutex; /** Mutex for locking mod order */
    std::vector<std::wstring> m_modOrder; /** Stores the mod order configuration */
    std::unordered_map<std::wstring, int>
        m_modPriority; /** Stores the priority numbering for mods for constant time lookups */

    nlohmann::json m_userConfig; /** Stores the user config JSON object */

public:
    /**
     * @brief Loads required statics for the class
     *
     * @param exePath Path to ParallaxGen.exe (folder)
     */
    static void loadStatics(const std::filesystem::path& exePath);

    /**
     * @brief Get the User Config File object
     *
     * @return std::filesystem::path Path to user config file
     */
    [[nodiscard]] static auto getUserConfigFile() -> std::filesystem::path;

    /**
     * @brief Loads the config files in the `cfg` folder
     */
    void loadConfig();

    /**
     * @brief Get the Mod Order object
     *
     * @return std::vector<std::wstring> Mod order
     */
    [[nodiscard]] auto getModOrder() -> std::vector<std::wstring>;

    /**
     * @brief Get mod priority for a given mod
     *
     * @param mod mod to check
     * @return int priority of mod (-1 if not found)
     */
    [[nodiscard]] auto getModPriority(const std::wstring& mod) -> int;

    /**
     * @brief Get the Mod Priority Map object
     *
     * @return std::unordered_map<std::wstring, int> Mod priority map
     */
    [[nodiscard]] auto getModPriorityMap() -> std::unordered_map<std::wstring, int>;

    /**
     * @brief Set the Mod Order object (also saves to user json)
     *
     * @param modOrder new mod order to set
     */
    void setModOrder(const std::vector<std::wstring>& modOrder);

    /**
     * @brief Get the current Params
     *
     * @return PGParams params
     */
    [[nodiscard]] auto getParams() const -> PGParams;

    /**
     * @brief Set params (also saves to user json)
     *
     * @param params new params to set
     */
    void setParams(const PGParams& params);

    /**
     * @brief Validates a given param struct
     *
     * @param params Params to validate
     * @param errors Error messages
     * @return true no validation errors
     * @return false validation errors
     */
    [[nodiscard]] static auto validateParams(const PGParams& params, std::vector<std::string>& errors) -> bool;

    /**
     * @brief Get the Default Params object
     *
     * @return PGParams default params
     */
    [[nodiscard]] static auto getDefaultParams() -> PGParams;

    /**
     * @brief Get the User Config JSON object
     *
     * @return nlohmann::json User config JSON
     */
    [[nodiscard]] auto getUserConfigJSON() const -> nlohmann::json;

    /**
     * @brief Saves user config to the user json file
     */
    void saveUserConfig();

private:
    /**
     * @brief Parses JSON from a file
     *
     * @param bytes Bytes to parse
     * @param j parsed JSON object
     * @return true no json errors
     * @return false unable to parse
     */
    static auto parseJSON(const std::vector<std::byte>& bytes, nlohmann::json& j) -> bool;

    /**
     * @brief Adds a JSON config to the current config
     *
     * @param j JSON object to add
     */
    auto addConfigJSON(const nlohmann::json& j) -> void;

    /**
     * @brief Replaces any / with \\ in a JSON object
     *
     * @param j JSON object to change
     */
    static void replaceForwardSlashes(nlohmann::json& j);
};
