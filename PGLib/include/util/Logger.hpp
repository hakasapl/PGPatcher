#pragma once

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

    template <typename... Args> static auto shouldLogString(const std::wstring& fmt, Args&&... moreArgs) -> bool
    {
        const auto resultLog = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        return processMessage(resultLog);
    }

    template <typename... Args> static auto shouldLogString(const std::string& fmt, Args&&... moreArgs) -> bool
    {
        const std::string resolvedNarrow = fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
        const std::wstring resolvedWide(resolvedNarrow.begin(), resolvedNarrow.end());
        return processMessage(resolvedWide);
    }

public:
    // Scoped prefix class
    class Prefix {
    public:
        explicit Prefix(const std::wstring& prefix);
        explicit Prefix(const std::string& prefix);
        ~Prefix();
        Prefix(const Prefix&) = delete;
        auto operator=(const Prefix&) -> Prefix& = delete;
        Prefix(Prefix&&) = delete;
        auto operator=(Prefix&&) -> Prefix& = delete;
    };

    // MT helpers
    static void startThreadedBuffer();
    static void flushThreadedBuffer();

    // WString Log functions
    template <typename... Args> static void critical(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::critical, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::critical(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void error(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::err, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::error(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void warn(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::warn, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::warn(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void info(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::info, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::info(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void debug(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::debug,
                fmt::format(fmt::runtime(buildPrefixWString() + fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::debug(fmt::runtime(buildPrefixWString() + fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void trace(const std::wstring& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::trace,
                fmt::format(fmt::runtime(buildPrefixWString() + fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::trace(fmt::runtime(buildPrefixWString() + fmt), std::forward<Args>(moreArgs)...);
    }

    // String Log functions
    template <typename... Args> static void critical(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::critical, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::critical(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void error(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::err, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::error(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void warn(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (!shouldLogString(fmt, std::forward<Args>(moreArgs)...)) {
            return;
        }

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::warn, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::warn(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void info(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(
                spdlog::level::info, fmt::format(fmt::runtime(fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::info(fmt::runtime(fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void debug(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::debug,
                fmt::format(fmt::runtime(buildPrefixString() + fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::debug(fmt::runtime(buildPrefixString() + fmt), std::forward<Args>(moreArgs)...);
    }

    template <typename... Args> static void trace(const std::string& fmt, Args&&... moreArgs)
    {
        const std::shared_lock lock(s_mtLogLock);

        if (s_isThreadedBufferActive) {
            s_curBuffer.emplace_back(spdlog::level::trace,
                fmt::format(fmt::runtime(buildPrefixString() + fmt), std::forward<Args>(moreArgs)...));
            return;
        }

        spdlog::trace(fmt::runtime(buildPrefixString() + fmt), std::forward<Args>(moreArgs)...);
    }
};
