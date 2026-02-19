#pragma once

#include "PGConfig.hpp"

class PGUI {
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
