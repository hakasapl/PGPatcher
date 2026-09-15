#pragma once

#include "GUI/WXLoggerSink.hpp"
#include "PGConfig.hpp"

#include <filesystem>
#include <memory>
#include <mutex>

/**
 * @brief Static global singleton that stores application-level globals.
 *
 * Holds the PGConfig instance, the WXLoggerSink, the executable path, and the
 * dark-mode flag. All constructors are deleted; access is through static methods only.
 */
class PGPatcherGlobals {
private:
    static PGConfig* s_pgc;
    static std::shared_ptr<WXLoggerSink<std::mutex>> s_wxLoggerSink;

    static std::filesystem::path s_exePath;

    static bool s_isDarkMode;

public:
    PGPatcherGlobals() = delete;
    ~PGPatcherGlobals() = delete;
    PGPatcherGlobals(const PGPatcherGlobals&) = delete;
    PGPatcherGlobals& operator=(const PGPatcherGlobals&) = delete;
    PGPatcherGlobals(PGPatcherGlobals&&) = delete;
    PGPatcherGlobals& operator=(PGPatcherGlobals&&) = delete;

    /**
     * @brief Get the global PGConfig pointer.
     *
     * @return Pointer to the current PGConfig instance, or nullptr if not set.
     */
    static PGConfig* pgc();

    /**
     * @brief Set the global PGConfig pointer.
     *
     * @param pgc Pointer to the PGConfig instance to store globally.
     */
    static void setPGC(PGConfig* pgc);

    /**
     * @brief Get the path to the running executable.
     *
     * @return Filesystem path of the executable directory.
     */
    static std::filesystem::path exePath();

    /**
     * @brief Set the path to the running executable.
     *
     * @param exePath Filesystem path to store as the executable path.
     */
    static void setEXEPath(const std::filesystem::path& exePath);

    /**
     * @brief Get the shared WXLoggerSink instance.
     *
     * @return Shared pointer to the WXLoggerSink used for UI log capture.
     */
    static std::shared_ptr<WXLoggerSink<std::mutex>> wxLoggerSink();

    /**
     * @brief Set the shared WXLoggerSink instance.
     *
     * @param sink Shared pointer to the WXLoggerSink to store globally.
     */
    static void setWXLoggerSink(std::shared_ptr<WXLoggerSink<std::mutex>> sink);

    /**
     * @brief Check whether the application is running in dark mode.
     *
     * @return true if dark mode is active, false otherwise.
     */
    static bool isDarkMode();

    /**
     * @brief Set the application dark-mode flag.
     *
     * @param isDarkMode true to enable dark mode, false to disable it.
     */
    static void setIsDarkMode(bool isDarkMode);
};
