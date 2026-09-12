#pragma once

#include "PGConfig.hpp"

#include <wx/iconbndl.h>

#include <string>

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
    static auto applyTheme() -> bool;

public:
    /**
     * @brief Initialize the wxWidgets UI framwork
     */
    static void init();

    /**
     * @brief Shows the launcher dialog to the user (Hangs thread until user presses okay)
     *
     * @param pgc Config object backing the UI
     * @param[in,out] params Params to show in the UI, updated with the values set by the user
     * @return true if the user chose "Update Output" (update the previous output in place), false for "Start Patching"
     */
    static auto showLauncher(PGConfig& pgc,
                             PGConfig::PGParams& params) -> bool;

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
    static auto getAppIcons() -> wxIconBundle;
};
