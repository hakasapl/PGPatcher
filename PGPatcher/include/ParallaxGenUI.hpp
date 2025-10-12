#pragma once

#include "ParallaxGenConfig.hpp"

class ParallaxGenUI {
public:
    /**
     * @brief Initialize the wxWidgets UI framwork
     */
    static void init();

    /**
     * @brief Shows the launcher dialog to the user (Hangs thread until user presses okay)
     *
     * @param OldParams Params to show in the UI
     * @return ParallaxGenConfig::PGParams Params set by the user
     */
    static void showLauncher(ParallaxGenConfig& pgc, ParallaxGenConfig::PGParams& params);

    /**
     * @brief Shows the mod selection dialog to the user (Hangs thread until user presses okay)
     */
    static void selectModOrder();
};
