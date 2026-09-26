#pragma once

#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"

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
 * @class PGConfig
 * @brief Class responsible for PGPatcherruntime configuration and JSON file interaction
 */
class PGConfig {
public:
    /**
     * @struct PGParams
     * @brief Struct that holds all the user-configurable parameters for ParallaxGen
     */
    struct PGParams {
        // Game.
        struct Game {
            std::filesystem::path dir;
            BethesdaGame::GameType type = BethesdaGame::GameType::SkyrimSE;

            bool operator==(const Game& other) const { return dir == other.dir && type == other.type; }
        } game;

        // Mod Manager.
        struct ModManager {
            PGModManager::ModManagerType type = PGModManager::ModManagerType::None;
            std::filesystem::path mo2InstanceDir;
            bool shouldUseMO2LooseFileOrder = true;

            bool operator==(const ModManager& other) const
            {
                return type == other.type && mo2InstanceDir == other.mo2InstanceDir
                    && shouldUseMO2LooseFileOrder == other.shouldUseMO2LooseFileOrder;
            }
        } modManager;

        // Output.
        struct Output {
            std::filesystem::path dir;
            bool zip = false;
            PGPlugin::PluginLang pluginLang = PGPlugin::PluginLang::English;

            bool operator==(const Output& other) const
            {
                return dir == other.dir && zip == other.zip && pluginLang == other.pluginLang;
            }
        } output;

        // Processing.
        struct Processing {
            bool multithread = true;
            bool enableModDevMode = false;
            bool enableDebugLogging = false;
            bool enableTraceLogging = false;
            std::unordered_set<PGPlugin::ModelRecordType> allowedModelRecordTypes = PGPlugin::defaultRecTypeSet();
            std::vector<std::wstring> vanillaBSAList;
            std::vector<std::pair<std::wstring, PGEnums::TextureType>> textureMaps;
            std::vector<std::wstring> allowList;
            std::vector<std::wstring> blockList;

            bool operator==(const Processing& other) const
            {
                return multithread == other.multithread && enableModDevMode == other.enableModDevMode
                    && enableDebugLogging == other.enableDebugLogging && enableTraceLogging == other.enableTraceLogging
                    && allowedModelRecordTypes == other.allowedModelRecordTypes
                    && vanillaBSAList == other.vanillaBSAList && textureMaps == other.textureMaps
                    && allowList == other.allowList && blockList == other.blockList;
            }
        } processing;

        // Pre-Patchers.
        struct PrePatcher {
            bool isFixMeshLightingEnabled = false;

            bool operator==(const PrePatcher& other) const
            {
                return isFixMeshLightingEnabled == other.isFixMeshLightingEnabled;
            }
        } prePatcher;

        // Shader Patchers.
        struct ShaderPatcher {
            bool isParallaxEnabled = true;
            bool isComplexMaterialEnabled = true;
            bool isTruePBREnabled = false;

            bool operator==(const ShaderPatcher& other) const
            {
                return isParallaxEnabled == other.isParallaxEnabled
                    && isComplexMaterialEnabled == other.isComplexMaterialEnabled
                    && isTruePBREnabled == other.isTruePBREnabled;
            }
        } shaderPatcher;

        // Shader Transforms.
        struct ShaderTransforms {
            bool isParallaxToCMEnabled = false;

            bool operator==(const ShaderTransforms& other) const
            {
                return isParallaxToCMEnabled == other.isParallaxToCMEnabled;
            }
        } shaderTransforms;

        // Post-Patchers.
        struct PostPatcher {
            bool disablePrePatchedMaterials = true;
            bool isFixSSSEnabled = false;
            bool isHairFlowMapEnabled = false;

            bool operator==(const PostPatcher& other) const
            {
                return disablePrePatchedMaterials == other.disablePrePatchedMaterials
                    && isFixSSSEnabled == other.isFixSSSEnabled && isHairFlowMapEnabled == other.isHairFlowMapEnabled;
            }
        } postPatcher;

        // Global Patchers.
        struct GlobalPatcher {
            bool isRecalculateBoundsEnabled = false;

            bool operator==(const GlobalPatcher& other) const
            {
                return isRecalculateBoundsEnabled == other.isRecalculateBoundsEnabled;
            }
        } globalPatcher;

        bool operator==(const PGParams& other) const
        {
            return game == other.game && modManager == other.modManager && output == other.output
                && processing == other.processing && prePatcher == other.prePatcher
                && shaderPatcher == other.shaderPatcher && shaderTransforms == other.shaderTransforms
                && postPatcher == other.postPatcher && globalPatcher == other.globalPatcher;
        }

        bool operator!=(const PGParams& other) const { return !(*this == other); }
    };

private:
    static std::filesystem::path s_exePath; /** Stores the ExePath of ParallaxGen.exe */

    PGParams m_params; /** Stores the configured parameters */

    std::string m_uiLanguage = "en"; /** Stores the GUI language code (not a patching param) */

    std::string m_uiTheme = "system"; /** Stores the GUI theme: "light", "dark", or "system" (not a patching param) */

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
    [[nodiscard]] static std::filesystem::path userConfigFile();

    /**
     * @brief Get the Mod Config File object
     *
     * @return std::filesystem::path Path to mod config file
     */
    [[nodiscard]] static std::filesystem::path modConfigFile();

    /**
     * @brief Get the Ignored Messages Config File object
     *
     * @return std::filesystem::path Path to ignored messages config file
     */
    [[nodiscard]] static std::filesystem::path ignoredMessagesConfigFile();

    /**
     * @brief Resolves a path from the config that may be relative to the PGPatcher.exe folder
     *
     * Relative paths in settings.json (e.g. an MO2 instance location of "..\\MO2") are relative to the folder
     * containing PGPatcher.exe rather than to the working directory, so a PGPatcher shipped inside a modlist folder
     * keeps working when that folder is moved. The stored value is left as-is; call this where the path is used.
     *
     * @param path Path from the config (absolute or relative)
     * @return std::filesystem::path Absolute, lexically normalized path; empty and absolute inputs are returned
     * unchanged
     */
    [[nodiscard]] static std::filesystem::path resolveExeRelativePath(const std::filesystem::path& path);

    /**
     * @brief Resolves the paths in params that may be relative to the PGPatcher.exe folder, in place
     *
     * Applies resolveExeRelativePath() to the MO2 instance location, the output location and the game location. The
     * game location is skipped when MO2 provides it (the launcher locks it to the value from modorganizer.ini): that
     * value is relative to the MO2 folder instead and is resolved by PGModManager::resolveMO2GamePath() when read.
     *
     * @param params Params to resolve (the stored config keeps the values as typed; call this on the copy that is used)
     */
    static void resolveRelativePaths(PGParams& params);

    /**
     * @brief Loads the config files in the `cfg` folder
     */
    void loadConfig();

    /**
     * @brief Get the current Params
     *
     * @return PGParams params
     */
    [[nodiscard]] PGParams params() const;

    /**
     * @brief Set params (also saves to user json)
     *
     * @param params new params to set
     */
    void setParams(const PGParams& params);

    /**
     * @brief Get the GUI language code (e.g. "en")
     */
    [[nodiscard]] std::string uiLanguage() const;

    /**
     * @brief Set the GUI language code (persisted on the next saveUserConfig)
     */
    void setUILanguage(const std::string& lang);

    /**
     * @brief Get the GUI theme ("light", "dark", or "system")
     */
    [[nodiscard]] std::string uiTheme() const;

    /**
     * @brief Set the GUI theme (persisted on the next saveUserConfig)
     */
    void setUITheme(const std::string& theme);

    /**
     * @brief Validates a given param struct
     *
     * Paths are validated after resolveRelativePaths(), so relative paths are accepted (see resolveExeRelativePath()).
     *
     * @param rawParams Params to validate, as stored in the config
     * @param errors Error messages (UTF-8)
     * @return true no validation errors
     * @return false validation errors
     */
    [[nodiscard]] static bool validateParams(const PGParams& rawParams,
                                             std::vector<std::string>& errors);

    /**
     * @brief Get the Default Params object
     *
     * @return PGParams default params
     */
    [[nodiscard]] static PGParams defaultParams();

    /**
     * @brief Get the User Config JSON object
     *
     * @return nlohmann::json User config JSON
     */
    [[nodiscard]] nlohmann::json userConfigJSON() const;

    /**
     * @brief Saves user config to the user json file
     *
     * @return true if save was successful
     */
    bool saveUserConfig();

    /**
     * @brief Saves the current mod configuration to modrules.json
     *
     * @return true if save was successful
     */
    static bool saveModConfig();

    static std::unordered_map<wxString,
                              bool>
    ignoredMessagesConfig();
    static bool saveIgnoredMessagesConfig(const std::unordered_map<wxString,
                                                                   bool>& ignoredItems);

private:
    /**
     * @brief Parses JSON from a file
     *
     * @param bytes Bytes to parse
     * @param j parsed JSON object
     * @return true no json errors
     * @return false unable to parse
     */
    static bool parseJSON(const std::vector<std::byte>& bytes,
                          nlohmann::json& j);

    /**
     * @brief Adds a JSON config to the current config
     *
     * @param j JSON object to add
     */
    void addConfigJSON(const nlohmann::json& j);

    /**
     * @brief Replaces any / with \\ in a JSON object
     *
     * @param j JSON object to change
     */
    static void replaceForwardSlashes(nlohmann::json& j);
};
