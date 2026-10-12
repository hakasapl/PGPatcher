#pragma once

#include "CMCache.hpp"
#include "Environment.hpp"
#include "PGD3D.hpp"
#include "PGParams.hpp"
#include "PatchStream.hpp"
#include "Plan.hpp"
#include "RecordEdits.hpp"
#include "Settings.hpp"
#include "TextureGenerator.hpp"
#include "Uses.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace PGLite {

class Menu;

/**
 * @brief The lifecycle of the plugin (DESIGN.md sections 4.1, 4.8 and 4.9): the checks at plugin load, the data-load
 * task that reads the uses, plans and registers the streams, the background initialization of the environment, the
 * bytes every stream serves, and Apply.
 */
class Runtime final : public PatchStream::Provider, public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
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

    /// @brief The record model uses of the loaded game, empty before the data-load task ran.
    [[nodiscard]] const Uses& uses() const { return m_uses; }

    /// @brief The values of PGLite.ini.
    [[nodiscard]] const Settings::Ini& ini() const { return m_ini; }

    /// @brief The parameters the current plan and environment were built from.
    [[nodiscard]] PGParams params() const;

    /// @brief Why PGLite is disabled, or nothing while it is active.
    [[nodiscard]] std::optional<std::string> disableReason() const;

    /// @brief Whether the data-load task registered its streams.
    [[nodiscard]] bool isActive() const { return m_isActive; }

    /// @brief Whether the background initialization finished, so patches can run.
    [[nodiscard]] bool isReady() const { return m_isReady; }

    /// @brief The toggle of the status page, persisted in settings.json. Any thread.
    [[nodiscard]] bool isEnabled() const { return m_isEnabled; }

    /// @brief The generation of the current plan; deferred record edits of an older plan do nothing.
    [[nodiscard]] unsigned planGeneration() const { return m_planGeneration; }

    /// @brief The current environment, nullptr before the background initialization finished. Main thread.
    [[nodiscard]] Environment* environment() const { return m_environment.get(); }

    [[nodiscard]] Status status() const;

    /// @brief The step the background initialization or Apply is at as a translation key, nullptr when neither runs.
    /// Any thread.
    [[nodiscard]] const char* stage() const { return m_stage; }

    /**
     * @brief Apply (DESIGN.md section 4.8): restores the records, rebuilds the environment and the plan with fresh
     * variant numbers and registers the new variant streams. The caller saves the settings first. Runs on the menu's
     * worker thread while the game's main thread waits inside Present, so no 3D is built while the records change;
     * blocks until in-flight patches finished.
     *
     * @param isEnabled false restores the records and installs an empty plan instead, so every stream serves the
     * original mesh until the next apply with true.
     * @return false if PGLite is not ready.
     */
    bool apply(const PGParams& params,
               bool isEnabled);

    // PatchStream::Provider.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> bytesFor(const std::string& path,
                                                                         SourceStream& source) override;

    // BSTEventSink<MenuOpenCloseEvent>: the development aid that loads a save when the main menu opens.
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>* source) override;

private:
    struct StreamEntry {
        RE::BSTSmartPointer<RE::BSResource::Stream> stream;
        std::shared_ptr<SourceStream> source;
    };

    Runtime();

    static void onMessage(SKSE::MessagingInterface::Message* message);

    /// @brief The task posted at data load; runs on the main thread one frame later.
    void onDataLoaded();

    /// @brief The conditions of DESIGN.md section 4.9 that need the loaded game, or nothing.
    [[nodiscard]] std::optional<std::string> detectDisableCondition() const;

    /// @brief Logs the reason, shows the notice and keeps PGLite inert.
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

    /// @brief The facegen paths of every NPC form. Main thread.
    [[nodiscard]] std::vector<std::filesystem::path> facegenPaths() const;

    /// @brief The background initialization: the GPU, the cache, the facegen streams, then the environment.
    void initializeInBackground();

    /// @brief Blocks until the background initialization finished.
    void waitUntilReady();

    Settings::Ini m_ini;
    PGParams m_params;
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
    CMCache m_cmCache;
    std::unique_ptr<Environment> m_environment;
    std::unique_ptr<Menu> m_menu;

    std::unordered_map<std::string, StreamEntry> m_streams;
    mutable std::mutex m_streamsMutex;

    /// @brief Shared by every patch, exclusive for Apply.
    std::shared_mutex m_pipelineLock;

    std::atomic<bool> m_isActive { false };
    std::atomic<bool> m_isReady { false };
    std::atomic<bool> m_isEnabled { true };
    std::atomic<const char*> m_stage { nullptr };
    std::atomic<bool> m_didLoadSaveAtStart { false };
    std::mutex m_readyMutex;
    std::condition_variable m_readyCondition;
};

}
