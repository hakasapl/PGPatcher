#pragma once

#include <filesystem>

namespace PGLite {

class Plan;

/**
 * @brief The runtime counterpart of the offline Light Placer handler patcher (DESIGN.md section 4.13).
 *
 * Light Placer attaches the lights of a config entry by the model string of the record whose 3D loads, and it reads
 * its configs under Data\LightPlacer at its plugin load. A record PGLite points at a variant path has another model
 * string, so its lights would be lost. Offline, HandlerLightPlacerTracker appends every duplicate's path to the
 * entries that list the original. At runtime the bridge writes one config of its own with the same lights for the
 * variant strings and asks Light Placer to read its configs again through its ReloadLP console command, after data
 * load and after every Apply. The config stays on disk, so the next session's Light Placer reads it at plugin load.
 */
class LightPlacerBridge {
public:
    /// @brief Whether Light Placer is loaded (po3_LightPlacer.dll).
    [[nodiscard]] static bool isLightPlacerLoaded();

    /**
     * @brief Writes the config of the variants of the plan, or deletes it when no entry lists a model with variants,
     * and asks Light Placer to reload its configs on the main thread once the current step is done. Nothing happens
     * without Light Placer. Never throws: a failure is logged.
     */
    static void update(const Plan& plan);

private:
    /// @brief Data\LightPlacer\PGLite_Variants.json.
    [[nodiscard]] static std::filesystem::path configFile();

    /// @brief Posts the task that runs Light Placer's reload command.
    static void requestReload();
};

}
