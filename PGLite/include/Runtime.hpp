#pragma once

#include "ClassificationCache.hpp"
#include "Environment.hpp"
#include "OfflineOutput.hpp"
#include "PGD3D.hpp"
#include "PGParams.hpp"
#include "PatchStream.hpp"
#include "Plan.hpp"
#include "RecordEdits.hpp"
#include "Settings.hpp"
#include "ShaderFrameworks.hpp"
#include "TextureGenerator.hpp"
#include "Uses.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

class Menu;

/**
 * @brief The lifecycle of the plugin (DESIGN.md sections 4.1, 4.8 and 4.9): the checks at plugin load, the data-load
 * work that reads the uses, plans and registers the streams, the background initialization of the environment, the
 * bytes every stream serves, and Apply.
 */
class Runtime final : public PatchStream::Provider {
public:
    /// @brief What the GUI shows about the state of the plugin.
    struct Status {
        bool isActive = false;
        bool isReady = false;
        /// @brief The toggle of the status page: false means every stream serves the original mesh.
        bool isEnabled = true;
        std::optional<std::string> disableReason;
        std::size_t uses = 0;
        std::size_t usedMeshes = 0;
        std::size_t families = 0;
        std::size_t facegenFamilies = 0;
        std::size_t patchedFamilies = 0;
        std::size_t variants = 0;
        std::size_t streams = 0;
        std::size_t textureSets = 0;
        /// @brief The generated textures of the session (DESIGN.md section 4.12).
        std::size_t texturesFromCache = 0;
        std::size_t texturesGenerated = 0;
        std::size_t texturesFailed = 0;
        std::filesystem::path mo2Instance;
        /// @brief What the restoration of an enabled offline output did (DESIGN.md section 4.14), nothing without
        /// one.
        std::optional<OfflineOutput::Result> offlineOutput;
        /// @brief The step the background initialization or Apply is at as a translation key, nullptr when neither
        /// runs.
        const char* stage = nullptr;
    };

    /// @brief The one runtime of the process. It lives for the session and is never destroyed.
    static Runtime& instance();

    /**
     * @brief Plugin load: the log, the ini and the conditions that disable PGLite before the game exists. Always
     * returns true so that SKSE keeps the plugin loaded, disabled or not.
     */
    bool load();

    /// @brief The record model uses of the loaded game, empty before the data-load work ran.
    [[nodiscard]] const Uses& uses() const { return m_uses; }

    /// @brief The values of PGLite.ini.
    [[nodiscard]] const Settings::Ini& ini() const { return m_ini; }

    /// @brief The parameters the current plan and environment were built from.
    [[nodiscard]] PGParams params() const;

    /// @brief The shader frameworks found at data load: what the patcher options of the menu can offer.
    [[nodiscard]] const ShaderFrameworks::Detected& shaderFrameworks() const { return m_shaderFrameworks; }

    /// @brief Why PGLite is disabled, or nothing while it is active.
    [[nodiscard]] std::optional<std::string> disableReason() const;

    /// @brief Whether the data-load work registered its streams.
    [[nodiscard]] bool isActive() const { return m_isActive; }

    /// @brief Whether the background initialization finished, so patches can run.
    [[nodiscard]] bool isReady() const { return m_isReady; }

    /// @brief The toggle of the status page, persisted in settings.json. Any thread.
    [[nodiscard]] bool isEnabled() const { return m_isEnabled; }

    /// @brief The generation of the current plan; deferred record edits of an older plan do nothing.
    [[nodiscard]] unsigned planGeneration() const { return m_planGeneration; }

    /// @brief The current environment, nullptr before the background initialization finished and while PGLite is
    /// disabled in the settings. Main thread.
    [[nodiscard]] Environment* environment() const { return m_environment.get(); }

    /// @brief The snapshot of the record fields, which every model string write goes through (RecordSnapshot).
    [[nodiscard]] RecordSnapshot& snapshot() { return m_snapshot; }

    [[nodiscard]] Status status() const;

    /// @brief The step the background initialization or Apply is at as a translation key, nullptr when neither runs.
    /// Any thread.
    [[nodiscard]] const char* stage() const { return m_stage; }

    /// @brief The progress of the step when it reports one (the meshes read so far and the meshes to read), zero
    /// totals otherwise. Any thread.
    [[nodiscard]] std::pair<std::size_t,
                            std::size_t>
    progress() const
    {
        return { m_progressDone, m_progressTotal };
    }

    /**
     * @brief Apply (DESIGN.md section 4.8): restores the records, rebuilds the environment and the plan with fresh
     * variant numbers and registers the new variant streams. The caller saves the settings first. Runs on the menu's
     * worker thread while the game's main thread waits inside Present, so no 3D is built while the records change;
     * blocks until in-flight patches finished.
     *
     * @param isEnabled false restores the records and installs an empty plan instead, so every stream serves the
     * original mesh until the next apply with true.
     * @return false if PGLite is not ready or the apply failed; a failed apply restores the records, installs an empty
     * plan and leaves PGLite disabled until the next apply.
     */
    bool apply(const PGParams& requestedParams,
               bool isEnabled);

    // PatchStream::Provider.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> bytesFor(const std::string& path,
                                                                         SourceStream& source) override;

private:
    struct StreamEntry {
        RE::BSTSmartPointer<RE::BSResource::Stream> stream;
        std::shared_ptr<SourceStream> source;
    };

    Runtime();

    static void onMessage(SKSE::MessagingInterface::Message* message);

    /// @brief The data-load work, after onMessage() checked the disable conditions: the uses, the plan, the streams,
    /// the background thread.
    void onDataLoaded();

    /**
     * @brief The input-loaded work, before the engine loads the plugins: the settings and, with PGLite enabled, the
     * environment on a background thread. It needs no record and its file map takes the longest of the startup, so
     * it is built while the plugins load; the data-load work joins it (initializeInBackground).
     */
    void onInputLoaded();

    /**
     * @brief Waits for the environment build started at the input load, if one runs: what it threw is logged. For
     * the paths that end the initialization without initializeInBackground(), which joins it itself.
     */
    void waitForEarlyInitialization();

    /// @brief Detects the shader frameworks once; the SKSE plugins and ENB's proxy are loaded before the input is.
    void detectShaderFrameworks();

    /// @brief Reads settings.json, or the first-start defaults, and the enabled flag, once.
    void readSettings();

    /**
     * @brief Blocks the data-load handler until the background initialization is done, so the main menu opens with
     * PGLite ready (DESIGN.md section 4.1). Gives up with a warning after a long wait.
     */
    void waitForInitialization() const;

    /// @brief The conditions of DESIGN.md section 4.9 that need the loaded game, or nothing.
    [[nodiscard]] std::optional<std::string> detectDisableCondition() const;

    /// @brief Logs the reason, shows the notice (on the main thread, once the game runs its tasks) and keeps PGLite
    /// inert: whoever waits for the initialization is released and every stream serves the original mesh.
    void disable(const std::string& reason,
                 bool shouldShowInGame);

    /**
     * @brief The engine's own stream for a path, resolved before any registration of the path replaced it.
     *
     * @return The source, or nullptr if the engine cannot resolve the path.
     */
    [[nodiscard]] std::shared_ptr<SourceStream> resolveSource(const std::filesystem::path& path);

    /**
     * @brief Registers a stream for a path unless one is registered already.
     */
    void registerStream(const std::filesystem::path& path,
                        std::shared_ptr<SourceStream> source);

    /// @brief Resolves the sources of every family of the plan and registers the streams of their members.
    void registerPlanStreams();

    /// @brief Adds the facegen families to the plan and registers their streams.
    void registerFacegenStreams();

    /// @brief The facegen paths of every NPC form. At data load, before the game runs.
    [[nodiscard]] std::vector<std::filesystem::path> facegenPaths() const;

    /**
     * @brief The background initialization of the data-load work: the facegen streams, then the environment, joined
     * from the input-loaded thread when it was started there and built here otherwise.
     */
    void initializeInBackground();

    /// @brief The GPU, the observer and the environment (buildEnvironment), on whichever thread builds them.
    void prepareEnvironment();
    void reportStage(const char* stage);
    void reportProgress(std::size_t done,
                        std::size_t total);

    /**
     * @brief Builds and installs the environment of the current parameters: the classification cache is read for its
     * mesh reading pass and dropped from memory once the pass saved it, and the generated textures of the session are
     * made known to the new directory. While no patch runs.
     */
    void buildEnvironment(const std::function<void(const char*)>& reportStage,
                          const Environment::ProgressCallback& reportProgress);

    /**
     * @brief Blocks until the background initialization finished, for as long as the main menu is held at most.
     *
     * @return false if it did not finish in time: the original mesh is served then, and later calls do not wait
     * again until it finishes.
     */
    [[nodiscard]] bool waitUntilReady();

    Settings::Ini m_ini;
    PGParams m_params;
    ShaderFrameworks::Detected m_shaderFrameworks;
    std::optional<OfflineOutput::Result> m_offlineOutput;
    mutable std::mutex m_stateMutex;
    std::optional<std::string> m_disableReason;

    Uses m_uses;
    RecordSnapshot m_snapshot;
    std::vector<std::filesystem::path> m_facegenPaths;
    std::unique_ptr<Plan> m_plan;
    std::atomic<unsigned> m_planGeneration { 0 };
    unsigned m_variantCounter = 0;
    TextureSetFactory m_textureSets;
    TextureGenerator m_textures;

    PGD3D m_d3d;
    ClassificationCache m_cache;
    std::unique_ptr<Environment> m_environment;
    std::unique_ptr<Menu> m_menu;
    /// @brief The environment build started at input load, joined by the data-load work; invalid when none was.
    std::future<void> m_earlyInitialization;
    bool m_didDetectShaderFrameworks = false;
    bool m_areSettingsRead = false;

    std::unordered_map<std::string, StreamEntry> m_streams;
    mutable std::mutex m_streamsMutex;
    /// @brief Whether the grass loader was told to take the registered grass streams (registerPlanStreams).
    bool m_didRefreshGrassStreams = false;

    /// @brief Shared by every patch, exclusive for Apply.
    std::shared_mutex m_pipelineLock;

    std::atomic<bool> m_isActive { false };
    std::atomic<bool> m_isReady { false };
    /// @brief Whether an open waited the whole hold for the initialization in vain.
    std::atomic<bool> m_didInitializationExpire { false };
    std::atomic<bool> m_isEnabled { true };
    std::atomic<const char*> m_stage { nullptr };
    std::atomic<std::size_t> m_progressDone { 0 };
    std::atomic<std::size_t> m_progressTotal { 0 };
    std::mutex m_readyMutex;
    std::condition_variable m_readyCondition;
};

}
