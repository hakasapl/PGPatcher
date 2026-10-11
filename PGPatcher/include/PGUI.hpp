#pragma once

#include "PGConfig.hpp"

#include <wx/iconbndl.h>

#include <string>

/**
 * @brief Entry points of the wxWidgets GUI: initializes the toolkit and shows the launcher and the mod order dialog
 */
class PGUI {
private:
    static inline std::string s_appliedTheme; /** Theme last applied to the native appearance */

    /**
     * @brief Applies the configured GUI theme.
     *        Must be called with no live top-level windows for the native appearance to change.
     *
     * @return true if the native appearance matches the requested theme; false if it could not be
     *         changed (wxMSW refuses appearance changes once dark mode was enabled in this process,
     *         so leaving dark/system mode requires a full application restart)
     */
    static bool applyTheme();

public:
    /**
     * @brief Initialize the wxWidgets UI framework (creates the wxApp) and apply the configured theme
     *
     * @throws std::runtime_error if wxWidgets fails to initialize
     */
    static void init();

    /**
     * @brief Shows the launcher dialog to the user (Hangs thread until user presses okay)
     *
     * The launcher is rebuilt after a language or theme change. PGPatcher restarts itself when the new theme cannot be
     * applied in this process.
     *
     * @param pgc Config object backing the UI
     * @param[in,out] params Params to show in the UI, updated with the values set by the user
     * @return true if the user chose "Update Output" (update the previous output in place), false for "Start Patching"
     */
    static bool showLauncher(PGConfig& pgc,
                             PGConfig::PGParams& params);

    /**
     * @brief Shows the mod selection dialog to the user (Hangs thread until user presses okay)
     */
    static void selectModOrder();

    /**
     * @brief PGPatcher application icon in every size of the icon resource, meant for wxTopLevelWindow::SetIcons so
     *        that the title bar and the taskbar entry of a window get the size matching the monitor's DPI
     *
     * @return wxIconBundle application icons
     */
    static wxIconBundle appIcons();
};
