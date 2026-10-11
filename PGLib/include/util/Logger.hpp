#pragma once

#include <spdlog/common.h>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

/**
 * @brief Thread-safe logging facade built on top of spdlog with deduplication and prefix support.
 *
 * Features:
 * - Duplicate-message suppression: identical messages (critical/error/warn) are logged only once.
 * - Scoped prefix stack: Prefix objects push/pop bracket-enclosed labels for debug/trace messages.
 * - Threaded buffer mode: worker threads accumulate log entries that are flushed atomically via
 *   flushThreadedBuffer() to avoid interleaved output.
 * - Overloads for both std::string (UTF-8) and std::wstring (UTF-16) format strings.
 *
 * The templates only format; everything else happens in log(), which lives in PGLib.dll together with all the state.
 * That way the executables and the DLL share one set of suppressed messages, one thread buffer and one capture
 * callback per thread (inline members would give every module its own copy).
 */
class Logger {
public:
    /**
     * @brief Callback type for capturing warning/error/critical messages emitted on the current thread.
     */
    using MessageCaptureFn = void (*)(spdlog::level::level_enum,
                                      const std::wstring&);

private:
    static std::unordered_set<std::wstring> s_existingMessages;
    static std::shared_mutex s_existingMessagesMutex;

    // Snapshot of s_existingMessages taken by markRunStart().
    static std::unordered_set<std::wstring> s_runStartMessages;
    static bool s_isRunStartMarked;

    // Held shared while a message is logged and exclusively while a thread buffer is flushed.
    static std::shared_mutex s_mtLogLock;
    thread_local static std::vector<std::pair<spdlog::level::level_enum, std::variant<std::wstring, std::string>>>
        s_curBuffer;
    thread_local static bool s_isThreadedBufferActive;
    thread_local static MessageCaptureFn s_threadMessageCapture;

    thread_local static std::vector<std::wstring> s_prefixStack;
    static std::wstring buildPrefixWString();
    static std::string buildPrefixString();

    /**
     * @brief Returns whether a message is logged for the first time, and remembers it.
     */
    static bool shouldLog(const std::wstring& message);

    /**
     * @brief Logs a formatted message: warnings and above are handed to the thread's capture callback and dropped
     * when they were logged before, then the message is buffered or passed to spdlog.
     */
    static void log(spdlog::level::level_enum level,
                    const std::wstring& message);

    /**
     * @brief UTF-8 variant of log().
     */
    static void log(spdlog::level::level_enum level,
                    const std::string& message);

public:
    /**
     * @brief RAII guard that pushes a label onto the thread-local prefix stack for debug/trace messages.
     *
     * The label is enclosed in square brackets and prepended to every debug/trace message logged
     * while this object is alive. The prefix is automatically removed when the object is destroyed.
     */
    class Prefix {
    public:
        /**
         * @brief Pushes a wide-string prefix label onto the thread-local prefix stack.
         *
         * @param prefix Label text to prepend to debug/trace messages.
         */
        explicit Prefix(const std::wstring& prefix);

        /**
         * @brief Pushes a UTF-8 string prefix label onto the thread-local prefix stack.
         *
         * @param prefix Label text to prepend to debug/trace messages.
         */
        explicit Prefix(const std::string& prefix);

        /**
         * @brief Pops this prefix label from the thread-local prefix stack.
         */
        ~Prefix();

        Prefix(const Prefix&) = delete;
        Prefix& operator=(const Prefix&) = delete;
        Prefix(Prefix&&) = delete;
        Prefix& operator=(Prefix&&) = delete;
    };

    /**
     * @brief Remembers the current set of suppressed duplicate messages as the start-of-run baseline.
     *
     * Call right before the patching step so a re-run of the patching step (see resetToRunStart()) can log the same
     * messages again while messages from the preparation phase stay suppressed.
     */
    static void markRunStart();

    /**
     * @brief Restores duplicate suppression to the state captured by markRunStart(), so messages logged during a
     * previous patching step are logged again when the step is re-run. No-op if markRunStart() was never called.
     */
    static void resetToRunStart();

    /**
     * @brief Sets (or clears with nullptr) a capture callback for the current thread.
     *
     * While set, every warning, error, and critical message logged on this thread is passed to the callback before
     * duplicate suppression and buffering, so the callback sees every message the thread attempted to log.
     *
     * @param captureFn Callback to invoke, or nullptr to disable capturing on this thread.
     */
    static void setThreadMessageCapture(MessageCaptureFn captureFn);

    /**
     * @brief Activates the thread-local buffered logging mode and clears any previous buffer contents.
     *
     * While active, all log calls on the current thread are stored in a per-thread buffer instead
     * of being forwarded to spdlog. Call flushThreadedBuffer() to emit the collected messages.
     */
    static void startThreadedBuffer();

    /**
     * @brief Flushes the thread-local log buffer to spdlog under a global exclusive lock and deactivates buffering.
     *
     * Acquires the global write lock so the entire batch of buffered messages is emitted without
     * interleaving with other threads.
     */
    static void flushThreadedBuffer();

    /**
     * @brief Logs a critical-level message using a wide format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void critical(const std::wstring& fmt,
                         Args&&... moreArgs)
    {
        log(spdlog::level::critical, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs an error-level message using a wide format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void error(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::err, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a warning-level message using a wide format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void warn(const std::wstring& fmt,
                     Args&&... moreArgs)
    {
        log(spdlog::level::warn, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs an info-level message using a wide format string (no deduplication).
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void info(const std::wstring& fmt,
                     Args&&... moreArgs)
    {
        log(spdlog::level::info, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a debug-level message using a wide format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void debug(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::debug,
            buildPrefixWString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a trace-level message using a wide format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void trace(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::trace,
            buildPrefixWString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a critical-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void critical(const std::string& fmt,
                         Args&&... moreArgs)
    {
        log(spdlog::level::critical, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs an error-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void error(const std::string& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::err, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a warning-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void warn(const std::string& fmt,
                     Args&&... moreArgs)
    {
        log(spdlog::level::warn, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs an info-level message using a narrow (UTF-8) format string (no deduplication).
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void info(const std::string& fmt,
                     Args&&... moreArgs)
    {
        log(spdlog::level::info, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a debug-level message using a narrow (UTF-8) format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void debug(const std::string& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::debug,
            buildPrefixString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }

    /**
     * @brief Logs a trace-level message using a narrow (UTF-8) format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template<typename... Args>
    static void trace(const std::string& fmt,
                      Args&&... moreArgs)
    {
        log(spdlog::level::trace,
            buildPrefixString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
    }
};
