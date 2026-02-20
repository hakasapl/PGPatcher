#pragma once

#include "util/StringUtil.hpp"
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <fmt/base.h>
#include <fmt/format.h>
#include <fmt/xchar.h>
#include <mutex>
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
 */
class Logger {
private:
    inline static std::unordered_set<std::wstring> s_existingMessages;
    inline static std::shared_mutex s_existingMessagesMutex;

    inline static std::shared_mutex s_mtLogLock;
    inline thread_local static std::vector<
        std::pair<spdlog::level::level_enum, std::variant<std::wstring, std::string>>>
        s_curBuffer;
    inline thread_local static bool s_isThreadedBufferActive;

    thread_local static std::vector<std::wstring> s_prefixStack;
    static auto buildPrefixWString() -> std::wstring;
    static auto buildPrefixString() -> std::string;

    static auto processMessage(const std::wstring& message) -> bool
    {
        {
            const std::shared_lock lock(s_existingMessagesMutex);
            if (s_existingMessages.contains(message)) {
                // don't log anything if already logged
                return false;
            }
        }

        {
            const std::unique_lock lock(s_existingMessagesMutex);
            const auto [_, inserted] = s_existingMessages.insert(message);
            return inserted; // true only for the first thread
        }
    }

    template <typename... Args> static auto shouldLogString(const std::wstring& fmt) -> bool
    {
        return processMessage(fmt);
    }

    template <typename... Args> static auto shouldLogString(const std::string& fmt) -> bool
    {
        return processMessage(StringUtil::utf8toUTF16(fmt));
    }

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
        auto operator=(const Prefix&) -> Prefix& = delete;
        Prefix(Prefix&&) = delete;
        auto operator=(Prefix&&) -> Prefix& = delete;
    };

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
    template <typename... Args>
    static void critical(const std::wstring& fmt,
                         Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::critical, resolvedStr);
            return;
        }

        spdlog::critical(L"{}", resolvedStr);
    }

    /**
     * @brief Logs an error-level message using a wide format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void error(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::err, resolvedStr);
            return;
        }

        spdlog::error(L"{}", resolvedStr);
    }

    /**
     * @brief Logs a warning-level message using a wide format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void warn(const std::wstring& fmt,
                     Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::warn, resolvedStr);
            return;
        }

        spdlog::warn(L"{}", resolvedStr);
    }

    /**
     * @brief Logs an info-level message using a wide format string (no deduplication).
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void info(const std::wstring& fmt,
                     Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::info, resolvedStr);
            return;
        }

        spdlog::info(L"{}", resolvedStr);
    }

    /**
     * @brief Logs a debug-level message using a wide format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void debug(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = buildPrefixWString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::debug, resolvedStr);
            return;
        }

        spdlog::debug(L"{}", resolvedStr);
    }

    /**
     * @brief Logs a trace-level message using a wide format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Wide-string fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void trace(const std::wstring& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = buildPrefixWString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::trace, resolvedStr);
            return;
        }

        spdlog::trace(L"{}", resolvedStr);
    }

    /**
     * @brief Logs a critical-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void critical(const std::string& fmt,
                         Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::critical, resolvedStr);
            return;
        }

        spdlog::critical("{}", resolvedStr);
    }

    /**
     * @brief Logs an error-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void error(const std::string& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::err, resolvedStr);
            return;
        }

        spdlog::error("{}", resolvedStr);
    }

    /**
     * @brief Logs a warning-level message using a narrow (UTF-8) format string, suppressing duplicates.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void warn(const std::string& fmt,
                     Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (!shouldLogString(resolvedStr)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::warn, resolvedStr);
            return;
        }

        spdlog::warn("{}", resolvedStr);
    }

    /**
     * @brief Logs an info-level message using a narrow (UTF-8) format string (no deduplication).
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void info(const std::string& fmt,
                     Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::info, resolvedStr);
            return;
        }

        spdlog::info("{}", resolvedStr);
    }

    /**
     * @brief Logs a debug-level message using a narrow (UTF-8) format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void debug(const std::string& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = buildPrefixString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::debug, resolvedStr);
            return;
        }

        spdlog::debug("{}", resolvedStr);
    }

    /**
     * @brief Logs a trace-level message using a narrow (UTF-8) format string, prepending the active prefix stack.
     *
     * @tparam Args Types of the format arguments.
     * @param fmt Narrow fmt format string.
     * @param moreArgs Arguments forwarded to fmt::format.
     */
    template <typename... Args>
    static void trace(const std::string& fmt,
                      Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        const auto resolvedStr = buildPrefixString() + fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::trace, resolvedStr);
            return;
        }

        spdlog::trace("{}", resolvedStr);
    }
};
