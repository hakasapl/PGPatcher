#pragma once

#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "Settings.hpp"
#include "common/MO2DataFolder.hpp"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace PGLite {

class ClassificationCache;

/**
 * @brief The runtime environment of the shared patcher code (DESIGN.md sections 4.5 and 4.7): the file map (read
 * natively for the mesh reading pass, through the engine's resource layer afterwards), the mesh reading pass and the
 * texture classification, the TruePBR configs, the mod manager and the registered patchers.
 *
 * One environment is built on a background thread during the plugin load (after the data load when the plugin order
 * cannot be read early) and replaced by Apply. The shared code reaches it through PGGlobals, so a new environment
 * installs itself there when it is built; the pipeline lock of the runtime keeps a patch from running while that
 * happens.
 */
class Environment {
public:
    /// @brief The texture of the package, the dynamic cubemap the complex material patcher sets (the offline program
    /// deploys it into its own output, so the mod that ships it is not a mod of the offline mod list).
    static constexpr std::wstring_view dynamicCubemapTexture = L"textures\\cubemaps\\dynamic1pxcubemap_black.dds";

    /**
     * @param excludedFiles Files the file map leaves out, lower case paths relative to the data folder: the textures
     * an enabled offline output generated (OfflineOutput).
     */
    Environment(PGParams params,
                Settings::Ini ini,
                std::unordered_set<std::filesystem::path> excludedFiles = { });
    ~Environment();
    Environment(const Environment&) = delete;
    Environment& operator=(const Environment&) = delete;
    Environment(Environment&&) = delete;
    Environment& operator=(Environment&&) = delete;

    /// @brief Receives (completed, total) counts while the meshes are read, for the GUI.
    using ProgressCallback = std::function<void(std::size_t, std::size_t)>;

    /**
     * @brief Builds everything: the mod manager, the file map, the mesh reading pass and the texture maps, the TruePBR
     * configs, the mod states and the patchers. Needs the GPU of PGGlobals::pgD3D() and the classification cache as
     * the installed observer.
     *
     * @param cache The classification cache, saved after the classification.
     * @param reportStage Called with the translation key of every step as it starts, for the GUI.
     * @param reportProgress Called with the progress of the mesh reading pass, for the GUI.
     * @throws std::runtime_error if the file map cannot be built.
     */
    void initialize(ClassificationCache& cache,
                    const std::function<void(const char*)>& reportStage = { },
                    const ProgressCallback& reportProgress = { });

    [[nodiscard]] const PGParams& params() const { return m_params; }
    [[nodiscard]] const Settings::Ini& ini() const { return m_ini; }
    [[nodiscard]] PGDirectory* directory() const { return m_directory.get(); }
    [[nodiscard]] PGModManager* modManager() const { return m_modManager.get(); }

    /// @brief The MO2 instance the environment used, empty for none.
    [[nodiscard]] const std::filesystem::path& mo2InstanceDir() const { return m_mo2InstanceDir; }

    /// @brief The configured MO2 instance as the runtime resolves it: a relative path is the game folder's, where the
    /// offline program's is its own folder's; an empty path stays empty.
    [[nodiscard]] static std::filesystem::path resolvedMO2InstanceDir(const std::filesystem::path& instanceDir);

    /**
     * @brief The archives the engine loads, in its order: the two INI lists, Update.bsa, the archives of the plugins
     * the engine loads without a Plugins.txt line (the masters and the Creation Club content) in their load order, then
     * those of the active lines of Plugins.txt in file order, light and regular plugins interleaved. Only archives that
     * exist in the data folder. Without a readable Plugins.txt the loaded plugins follow in their load order, light
     * plugins last, which needs the data load (canOrderArchivesEarly()); before it, the archives of the mods are not
     * mapped then, which is reported.
     */
    [[nodiscard]] static std::vector<std::wstring> engineArchiveOrder();

    /**
     * @brief Whether engineArchiveOrder() can be computed before the data load: Plugins.txt is readable.
     */
    [[nodiscard]] static bool canOrderArchivesEarly();

    /**
     * @brief Reports the active lines of Plugins.txt whose plugin the engine did not load: their archives were mapped
     * for nothing, and a mesh in them is served although the game never asks for it. Needs the data load.
     */
    static void reportArchivesOfUnloadedPlugins();

    /// @brief The active plugins file the engine reads, Plugins.txt of the local application data; under Mod
    /// Organizer 2 the file of the running profile.
    [[nodiscard]] static std::filesystem::path activePluginsFile();

    /**
     * @brief Registers the patchers the parameters enable with the shared pipeline (PGPatcher::loadPatchers).
     */
    static void registerPatchers(const PGParams& params,
                                 const Settings::Ini& ini);

private:
    /**
     * @brief Builds the view of the data folder from the configured Mod Organizer 2 instance, else the one that
     * launched the game, when the game runs under MO2's virtual file system. Without a view the data folder is
     * listed, which usvfs merges file by file.
     */
    void readMO2DataFolder();
    void initializeModManager();
    void initializeDirectory();
    void classifyTextures(ClassificationCache& cache,
                          const ProgressCallback& reportProgress);

    /**
     * @brief Makes every later read of the directory go through the engine's resource layer and drops the archive
     * tables. The mesh reading pass and the TruePBR configs read natively before this (DESIGN.md section 4.5).
     */
    void switchToEngineReads();

    PGParams m_params;
    Settings::Ini m_ini;
    std::unordered_set<std::filesystem::path> m_excludedFiles;
    std::filesystem::path m_mo2InstanceDir;
    std::unique_ptr<MO2DataFolder> m_mo2DataFolder; /**< The view the file maps are built from, until they are */
    std::unique_ptr<PGModManager> m_modManager;
    std::unique_ptr<PGDirectory> m_directory;
};

}
