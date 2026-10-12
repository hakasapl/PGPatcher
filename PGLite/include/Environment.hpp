#pragma once

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "Settings.hpp"
#include "Uses.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PGLite {

class CMCache;

/**
 * @brief The runtime environment of the shared patcher code (DESIGN.md sections 4.5 and 4.7): the textures-only file
 * map read through the engine's resource layer, the texture classification, the TruePBR configs, the mod manager and
 * the registered patchers.
 *
 * One environment is built on the background thread after data load and replaced by Apply. The shared code reaches it
 * through PGGlobals, so a new environment installs itself there when it is built; the pipeline lock of the runtime
 * keeps a patch from running while that happens.
 */
class Environment {
public:
    Environment(PGParams params,
                Settings::Ini ini);
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) = delete;
    Environment& operator=(Environment&&) = delete;

    /**
     * @brief Builds everything: the mod manager, the file map, the texture maps, the TruePBR configs, the mod states
     * and the patchers. Needs the GPU of PGGlobals::pgD3D() and the complex material cache as the installed observer.
     *
     * @param uses The record model uses, which decide which meshes stay in the file map and which mods have meshes.
     * @param cmCache The classification cache, saved after the classification.
     * @param reportStage Called with the translation key of every step as it starts, for the GUI.
     * @throws std::runtime_error if the file map cannot be built.
     */
    void initialize(const Uses& uses,
                    CMCache& cmCache,
                    const std::function<void(const char*)>& reportStage = { });

    /**
     * @brief Gives a mod manager built outside the environment what the classification and the runtime give the
     * installed one: the shaders of every mod from the classified textures and the TruePBR configs of the directory,
     * and which mods own a mesh with uses. The menu's live mod list uses it.
     */
    static void attributeMods(PGModManager& modManager,
                              PGDirectory& directory,
                              const Uses& uses);

    [[nodiscard]] const PGParams& params() const { return m_params; }
    [[nodiscard]] const Settings::Ini& ini() const { return m_ini; }
    [[nodiscard]] PGDirectory* directory() const { return m_directory.get(); }
    [[nodiscard]] PGModManager* modManager() const { return m_modManager.get(); }

    /// @brief The MO2 instance the environment used, empty for none.
    [[nodiscard]] const std::filesystem::path& mo2InstanceDir() const { return m_mo2InstanceDir; }

    /**
     * @brief The archives the engine loads, in load order: the two INI lists, Update.bsa, then the archives of every
     * loaded plugin. Only archives that exist in the data folder.
     */
    [[nodiscard]] static std::vector<std::wstring> engineArchiveOrder();

    /**
     * @brief Registers the patchers the parameters enable with the shared pipeline (PGPatcher::loadPatchers).
     */
    static void registerPatchers(const PGParams& params,
                                 const Settings::Ini& ini);

private:
    void initializeModManager(const Uses& uses);
    void initializeDirectory(const Uses& uses);
    void classifyTextures(CMCache& cmCache);

    PGParams m_params;
    Settings::Ini m_ini;
    std::filesystem::path m_mo2InstanceDir;
    std::unique_ptr<PGModManager> m_modManager;
    std::unique_ptr<PGDirectory> m_directory;
};

}
