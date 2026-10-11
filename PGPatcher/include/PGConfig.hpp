#pragma once

#include "PGModManager.hpp"
#include "PGParams.hpp"
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
 * @brief Holds the PGPatcher runtime configuration and reads and writes the JSON files of the cfg folder next to the
 * executable
 */
class PGConfig {
public:
    /// @brief The patching parameters, see PGParams (shared with the runtime).
    using PGParams = ::PGParams;

private:
    static std::filesystem::path s_exePath; /** Folder containing PGPatcher.exe */

    PGParams m_params; /** Stores the configured parameters */

    std::string m_uiLanguage = "en"; /** Stores the GUI language code (not a patching param) */

    std::string m_uiTheme = "system"; /** Stores the GUI theme: "light", "dark", or "system" (not a patching param) */

    nlohmann::json m_userConfig; /** Stores the user config JSON object */

public:
    /**
     * @brief Loads required statics for the class
     *
     * @param exePath Folder containing PGPatcher.exe; the cfg folder is resolved against it
     */
    static void loadStatics(const std::filesystem::path& exePath);

    /**
     * @brief Get the User Config File object
     *
     * @return std::filesystem::path Path to user config file (cfg\settings.json)
     * @throws std::runtime_error if loadStatics() was not called
     */
    [[nodiscard]] static std::filesystem::path userConfigFile();

    /**
     * @brief Get the Mod Config File object
     *
     * @return std::filesystem::path Path to mod config file (cfg\modrules.json)
     * @throws std::runtime_error if loadStatics() was not called
     */
    [[nodiscard]] static std::filesystem::path modConfigFile();

    /**
     * @brief Get the Ignored Messages Config File object
     *
     * @return std::filesystem::path Path to ignored messages config file (cfg\ignored_messages.json)
     * @throws std::runtime_error if loadStatics() was not called
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
     * @brief Loads the user config (settings.json). The params fall back to defaultParams() when the file is missing
     * or cannot be parsed
     */
    void loadConfig();

    /**
     * @brief Get the current Params
     *
     * @return PGParams params
     */
    [[nodiscard]] PGParams params() const;

    /**
     * @brief Set params in memory only (persisted on the next saveUserConfig)
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
     * @brief Saves user config (userConfigJSON()) to the user json file
     *
     * @return true if save was successful (a failure is logged as critical)
     */
    bool saveUserConfig();

    /**
     * @brief Saves the current mod configuration (PGGlobals::pgmm()->json()) to modrules.json
     *
     * @return true if save was successful (a failure is logged as critical)
     * @throws std::runtime_error if no mod manager is set
     */
    static bool saveModConfig();

    /**
     * @brief Reads the ignored warning messages from ignored_messages.json
     *
     * @return Message text to ignored flag; empty when the file does not exist
     * @throws std::runtime_error if the file cannot be parsed or has the wrong format
     */
    static std::unordered_map<wxString,
                              bool>
    ignoredMessagesConfig();

    /**
     * @brief Writes the ignored warning messages to ignored_messages.json
     *
     * @param ignoredItems Message text to ignored flag
     * @return true if save was successful (a failure is logged as critical)
     */
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
};
