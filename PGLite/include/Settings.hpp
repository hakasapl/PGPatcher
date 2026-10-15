#pragma once

#include "PGParams.hpp"

#include <nlohmann/json_fwd.hpp>
#include <spdlog/common.h>

#include <filesystem>
#include <string>

namespace PGLite {

/**
 * @brief The files of the plugin and their contents (DESIGN.md section 4.11).
 *
 * The ini (Data\SKSE\Plugins\PGLite.ini, next to the DLL) holds what the offline program takes from the command line.
 * settings.json holds the offline "params" object with the same keys, so settings can be copied between the programs,
 * and modrules.json the offline mod rules; they live under Data\SKSE\Plugins\PGLite with the caches and the
 * translations.
 */
class Settings {
public:
    /// @brief The values of PGLite.ini.
    struct Ini {
        /// @brief DirectInput scan code of the key that opens the menu.
        unsigned menuKey = defaultMenuKey;
        /// @brief Lowest log level written to PGLite.log.
        spdlog::level::level_enum logLevel = spdlog::level::info;
        /// @brief Whether facegen heads are left unpatched (--exclude-facegens offline).
        bool shouldExcludeFacegens = false;
        /// @brief Whether complex material shapes keep their cubemap (--disable-dyncubemap offline).
        bool shouldDisableDynamicCubemap = false;
        /// @brief Whether every parallax match is upgraded to complex material, not only those that cannot keep
        /// parallax (--force-always-cm offline).
        bool shouldForceAlwaysCM = false;
        /// @brief Whether "Loading PGLite" and the step show in the top right corner while PGLite initializes.
        bool shouldShowLoadingMessage = true;
        /// @brief Whether the welcome message shows in the top right corner until the menu was opened once.
        bool shouldShowWelcomeMessage = true;
    };

    /// @brief F8.
    static constexpr unsigned defaultMenuKey = 0x42;

    /// @brief The folder of the game executable.
    [[nodiscard]] static std::filesystem::path gameFolder();

    /// @brief The data folder of the game.
    [[nodiscard]] static std::filesystem::path dataFolder();

    /// @brief Data\SKSE\Plugins.
    [[nodiscard]] static std::filesystem::path pluginsFolder();

    /// @brief Data\SKSE\Plugins\PGLite, created when it does not exist.
    [[nodiscard]] static std::filesystem::path configFolder();

    [[nodiscard]] static std::filesystem::path iniFile();
    [[nodiscard]] static std::filesystem::path settingsFile();
    /// @brief Whether settings.json exists; a first start has none.
    [[nodiscard]] static bool hasSettingsFile();
    [[nodiscard]] static std::filesystem::path modRulesFile();
    [[nodiscard]] static std::filesystem::path classificationCacheFile();

    /// @brief Data\SKSE\Plugins\PGLite\translations, the translation files of the offline program.
    [[nodiscard]] static std::filesystem::path translationsFolder();

    /// @brief Data\SKSE\Plugins\PGLite\cache, the generated textures and their keys (DESIGN.md section 4.12). It can be
    /// deleted at any time at the cost of one generation per texture.
    [[nodiscard]] static std::filesystem::path textureCacheFolder();

    /**
     * @brief Reads PGLite.ini. A missing file or key gives the default.
     */
    [[nodiscard]] static Ini readIni();

    /**
     * @brief Reads the "params" object of settings.json, which holds every parameter as the offline program writes
     * it; the defaults of a fresh offline installation apply only when there is no file or no "params" object.
     */
    [[nodiscard]] static PGParams readParams();

    /**
     * @brief Writes the parameters into the "params" object of settings.json, keeping everything else in the file.
     *
     * @return false if the file could not be written (logged).
     */
    static bool writeParams(const PGParams& params);

    /// @brief Whether PGLite is enabled, the "pglite" object of settings.json. A missing file or key means enabled.
    [[nodiscard]] static bool readIsEnabled();

    /**
     * @brief Writes the enabled flag into the "pglite" object of settings.json, keeping everything else in the file.
     *
     * @return false if the file could not be written (logged).
     */
    static bool writeIsEnabled(bool isEnabled);

    /// @brief The language code of the "pglite" object of settings.json; empty (English) when there is none.
    [[nodiscard]] static std::string readLanguage();

    /**
     * @brief Writes the language code into the "pglite" object of settings.json, keeping everything else in the file.
     *
     * @return false if the file could not be written (logged).
     */
    static bool writeLanguage(const std::string& language);

    /// @brief Whether the menu was opened once, the "pglite" object of settings.json; the welcome message ends then.
    [[nodiscard]] static bool readDidOpenMenu();

    /**
     * @brief Records in the "pglite" object of settings.json that the menu was opened, keeping everything else. A
     * file without parameters yet (no Apply so far) gets the session's, so the defaults of a first start (the detected
     * mod manager, the patchers of the loaded shader framework) outlive the first look at the menu.
     *
     * @param params The session's parameters, written when the file has none.
     * @return false if the file could not be written (logged).
     */
    static bool writeDidOpenMenu(const PGParams& params);

    /**
     * @brief Reads modrules.json.
     *
     * @return false if there is no file or it is not valid JSON.
     */
    static bool readModRules(nlohmann::json& rules);

    /**
     * @brief Writes modrules.json.
     *
     * @return false if the file could not be written (logged).
     */
    static bool writeModRules(const nlohmann::json& rules);

    /**
     * @brief Writes a file through a temporary file next to it and a rename, so the file is complete or absent for
     * whoever reads it.
     *
     * @return false if the write failed; the temporary file is removed then.
     */
    [[nodiscard]] static bool writeFileAtomically(const std::filesystem::path& file,
                                                  const void* data,
                                                  std::size_t size);
};

}
