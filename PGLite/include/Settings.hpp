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
 * The ini (Data\SKSE\Plugins\PGLite.ini) holds what the offline program takes from the command line. settings.json
 * holds the offline "params" object with the same keys, so settings can be copied between the programs, and
 * modrules.json the offline mod rules. All of them live under Data\SKSE\Plugins\PGLite.
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
        /// @brief Whether complex material shapes keep their cubemap (--no-dyn-cubemap offline).
        bool shouldDisableDynamicCubemap = false;
        /// @brief Whether every parallax match is upgraded to complex material, not only those that cannot keep
        /// parallax (--force-always-cm offline).
        bool shouldForceAlwaysCM = false;
        /// @brief Whether "Loading PGLite" and the step show in the top right corner while PGLite initializes.
        bool shouldShowLoadingMessage = true;
        /// @brief Whether the welcome message shows in the top right corner until the menu was opened once.
        bool shouldShowWelcomeMessage = true;
        /// @brief A development aid: the save (file name without extension) loaded when the main menu opens.
        std::string loadSaveAtStart;
        /// @brief A development aid: open the menu once the runtime is ready, without the hotkey.
        bool shouldOpenMenuAtStart = false;
        /// @brief A development aid: run Apply once the menu opened at start, without a click.
        bool shouldApplyAtStart = false;
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
     * @brief Reads settings.json over the defaults of a fresh offline installation. Only the "params" object is read.
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
     * @brief Records in the "pglite" object of settings.json that the menu was opened, keeping everything else.
     *
     * @return false if the file could not be written (logged).
     */
    static bool writeDidOpenMenu();

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
};

}
