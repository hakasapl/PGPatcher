#pragma once

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenPlugin.hpp"
#include "util/NIFUtil.hpp"

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <wx/string.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
            bool mo2UseLooseFileOrder = true;

            auto operator==(const ModManager& other) const -> bool
            {
                return type == other.type && mo2InstanceDir == other.mo2InstanceDir
                    && mo2UseLooseFileOrder == other.mo2UseLooseFileOrder;
            }
        } ModManager;

        // Output
        struct Output {
            std::filesystem::path dir;
            bool zip = false;
            ParallaxGenPlugin::PluginLang pluginLang = ParallaxGenPlugin::PluginLang::ENGLISH;

            auto operator==(const Output& other) const -> bool
            {
                return dir == other.dir && zip == other.zip && pluginLang == other.pluginLang;
            }
        } Output;

        // Processing
        struct Processing {
            bool multithread = true;
            bool pluginESMify = false;
            bool enableModDevMode = false;
            bool enableDebugLogging = false;
            bool enableTraceLogging = false;
            std::unordered_set<ParallaxGenPlugin::ModelRecordType> allowedModelRecordTypes
                = ParallaxGenPlugin::getDefaultRecTypeSet();
            std::vector<std::wstring> vanillaBSAList;
            std::vector<std::pair<std::wstring, NIFUtil::TextureType>> textureMaps;
            std::vector<std::wstring> allowList;
            std::vector<std::wstring> blockList;

            auto operator==(const Processing& other) const -> bool
            {
                return multithread == other.multithread && pluginESMify == other.pluginESMify
                    && enableModDevMode == other.enableModDevMode && enableDebugLogging == other.enableDebugLogging
                    && enableTraceLogging == other.enableTraceLogging
                    && allowedModelRecordTypes == other.allowedModelRecordTypes
                    && vanillaBSAList == other.vanillaBSAList && textureMaps == other.textureMaps
                    && allowList == other.allowList && blockList == other.blockList;
            }
        } Processing;

        // Pre-Patchers
        struct PrePatcher {
            bool fixMeshLighting = false;

            auto operator==(const PrePatcher& other) const -> bool { return fixMeshLighting == other.fixMeshLighting; }
        } PrePatcher;

        // Shader Patchers
        struct ShaderPatcher {
            bool parallax = true;
            bool complexMaterial = true;
            bool truePBR = false;

            auto operator==(const ShaderPatcher& other) const -> bool
            {
                return parallax == other.parallax && complexMaterial == other.complexMaterial
                    && truePBR == other.truePBR;
            }
        } ShaderPatcher;

        // Shader Transforms
        struct ShaderTransforms {
            bool parallaxToCM = false;

            auto operator==(const ShaderTransforms& other) const -> bool { return parallaxToCM == other.parallaxToCM; }
        } ShaderTransforms;

        // Post-Patchers
        struct PostPatcher {
            bool disablePrePatchedMaterials = true;
            bool fixSSS = false;
            bool hairFlowMap = false;

            auto operator==(const PostPatcher& other) const -> bool
            {
                return disablePrePatchedMaterials == other.disablePrePatchedMaterials && fixSSS == other.fixSSS
                    && hairFlowMap == other.hairFlowMap;
            }
        } PostPatcher;

        // Global Patchers
        struct GlobalPatcher {
            bool fixEffectLightingCS = false;

            auto operator==(const GlobalPatcher& other) const -> bool
            {
                return fixEffectLightingCS == other.fixEffectLightingCS;
            }
        } GlobalPatcher;

        auto operator==(const PGParams& other) const -> bool
        {
            return Game == other.Game && ModManager == other.ModManager && Output == other.Output
                && Processing == other.Processing && PrePatcher == other.PrePatcher
                && ShaderPatcher == other.ShaderPatcher && ShaderTransforms == other.ShaderTransforms
                && PostPatcher == other.PostPatcher && GlobalPatcher == other.GlobalPatcher;
        }

        auto operator!=(const PGParams& other) const -> bool { return !(*this == other); }
    };

private:
    static std::filesystem::path s_exePath; /** Stores the ExePath of ParallaxGen.exe */

    PGParams m_params; /** Stores the configured parameters */

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
     * @brief Get the Mod Config File object
     *
     * @return std::filesystem::path Path to mod config file
     */
    [[nodiscard]] static auto getModConfigFile() -> std::filesystem::path;

    /**
     * @brief Get the Ignored Messages Config File object
     *
     * @return std::filesystem::path Path to ignored messages config file
     */
    [[nodiscard]] static auto getIgnoredMessagesConfigFile() -> std::filesystem::path;

    /**
     * @brief Loads the config files in the `cfg` folder
     */
    void loadConfig();

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
     *
     * @return true if save was successful
     */
    auto saveUserConfig() -> bool;

    /**
     * @brief Saves the current mod configuration to modrules.json
     *
     * @return true if save was successful
     */
    static auto saveModConfig() -> bool;

    static auto getIgnoredMessagesConfig() -> std::unordered_map<wxString, bool>;
    static auto saveIgnoredMessagesConfig(const std::unordered_map<wxString, bool>& ignoredItems) -> bool;

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
