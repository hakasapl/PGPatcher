#pragma once

#include "PGConfig.hpp"

#include <string>

class PGUI {
private:
    static inline bool s_forceDarkMode = false; /** CLI --force-dark override */
    static inline bool s_forceLightMode = false; /** CLI --force-light override */
    static inline std::string s_appliedTheme; /** Theme last applied to the native appearance */

    /**
     * @brief Applies the theme from the CLI overrides (if any) or the configured GUI theme.
     *        Must be called with no live top-level windows for the native appearance to change.
     *
     * @return true if the native appearance matches the requested theme; false if it could not be
     *         changed (wxMSW refuses appearance changes once dark mode was enabled in this process,
     *         so leaving dark/system mode requires a full application restart)
     */
    static auto applyTheme() -> bool;

public:
    /**
     * @brief Initialize the wxWidgets UI framwork
     */
    static void init(bool forceDarkMode = false,
                     bool forceLightMode = false);

    /**
     * @brief Shows the launcher dialog to the user (Hangs thread until user presses okay)
     *
     * @param OldParams Params to show in the UI
     * @return PGConfig::PGParams Params set by the user
     */
    static void showLauncher(PGConfig& pgc,
                             PGConfig::PGParams& params);

    /**
     * @brief Shows the mod selection dialog to the user (Hangs thread until user presses okay)
     */
    static void selectModOrder();
};
