#pragma once

#include <spdlog/common.h>

#include <string>
#include <vector>

namespace PGLite {

/**
 * @brief The log of the plugin: PGLite.log next to the other SKSE logs, written through the spdlog default logger that
 * the Logger facade of PGLib logs to.
 *
 * Every warning, error and critical message is also kept once for the GUI, whichever thread logged it.
 */
class Logging {
public:
    /// @brief One message of warning level or above, as the GUI shows it.
    struct Message {
        spdlog::level::level_enum level = spdlog::level::warn;
        std::string text;
    };

    /**
     * @brief Creates the log file and installs it as the default logger. Call once, at plugin load.
     *
     * @param level Lowest level that is written.
     * @return false if the SKSE log folder is unknown, in which case nothing is logged.
     */
    static bool initialize(spdlog::level::level_enum level);

    /**
     * @brief Returns the messages of warning level and above logged so far, each once, in order of first occurrence;
     * the oldest are dropped beyond a few thousand.
     */
    [[nodiscard]] static std::vector<Message> messages();

    /**
     * @brief Forgets the messages kept for the GUI.
     */
    static void clearMessages();

    /**
     * @brief Parses a log level name of the ini ("trace", "debug", "info", "warn", "error"); unknown names give info.
     */
    [[nodiscard]] static spdlog::level::level_enum levelFromString(const std::string& level);
};

}
