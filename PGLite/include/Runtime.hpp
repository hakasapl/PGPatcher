#pragma once

#include "PatchStream.hpp"
#include "Settings.hpp"
#include "Uses.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace PGLite {

/**
 * @brief The lifecycle of the plugin (DESIGN.md sections 4.1, 4.8 and 4.9): the checks at plugin load, the data-load
 * task that reads the uses and registers the streams, the background initialization, and the bytes every stream
 * serves.
 */
class Runtime final : public PatchStream::Provider {
public:
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

    /// @brief Why PGLite is disabled, or nothing while it is active.
    [[nodiscard]] std::optional<std::string> disableReason() const;

    /// @brief Whether the data-load task registered its streams.
    [[nodiscard]] bool isActive() const { return m_isActive; }

    /// @brief Whether the background initialization finished.
    [[nodiscard]] bool isReady() const { return m_isReady; }

    /// @brief The number of registered streams.
    [[nodiscard]] std::size_t streamCount() const;

    // PatchStream::Provider.
    [[nodiscard]] std::shared_ptr<const std::vector<std::byte>> bytesFor(const std::string& path,
                                                                        SourceStream& source) override;

private:
    Runtime() = default;

    static void onMessage(SKSE::MessagingInterface::Message* message);

    /// @brief The task posted at data load; runs on the main thread one frame later.
    void onDataLoaded();

    /// @brief The conditions of DESIGN.md section 4.9 that need the loaded game, or nothing.
    [[nodiscard]] std::optional<std::string> detectDisableCondition() const;

    /// @brief Logs the reason, shows the notice and keeps PGLite inert.
    void disable(const std::string& reason,
                 bool shouldShowInGame);

    /**
     * @brief Registers a stream for a path, wrapping the engine's own stream for it. A path the engine cannot resolve
     * gets no stream.
     *
     * @return true if a stream was registered.
     */
    bool registerStream(const std::string& path);

    /// @brief Registers a stream for every path with uses.
    void registerStreams();

    /// @brief The facegen paths of every NPC form, to be resolved in the background.
    [[nodiscard]] std::vector<std::string> facegenPaths() const;

    /// @brief The background initialization: the facegen streams, then the environment.
    void initializeInBackground(std::vector<std::string> facegenPaths);

    /// @brief Blocks until the background initialization finished.
    void waitUntilReady();

    Settings::Ini m_ini;
    mutable std::mutex m_stateMutex;
    std::optional<std::string> m_disableReason;
    Uses m_uses;

    std::unordered_map<std::string, RE::BSTSmartPointer<RE::BSResource::Stream>> m_streams;
    mutable std::mutex m_streamsMutex;

    std::atomic<bool> m_isActive { false };
    std::atomic<bool> m_isReady { false };
    std::mutex m_readyMutex;
    std::condition_variable m_readyCondition;
};

}
