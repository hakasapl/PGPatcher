#include "util/Logger.hpp"

#include "util/StringUtil.hpp"

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <variant>
#include <vector>

// Statics. Everything lives in this translation unit (PGLib.dll), see the class comment.
std::unordered_set<std::wstring> Logger::s_existingMessages;
std::shared_mutex Logger::s_existingMessagesMutex;
std::unordered_set<std::wstring> Logger::s_runStartMessages;
bool Logger::s_isRunStartMarked = false;
std::shared_mutex Logger::s_mtLogLock;
thread_local std::vector<std::pair<spdlog::level::level_enum, std::variant<std::wstring, std::string>>>
    Logger::s_curBuffer;
thread_local bool Logger::s_isThreadedBufferActive = false;
thread_local Logger::MessageCaptureFn Logger::s_threadMessageCapture = nullptr;
thread_local std::vector<std::wstring> Logger::s_prefixStack;

// Helper function to build the full prefix string.
std::wstring Logger::buildPrefixWString()
{
    std::wstringstream fullPrefix;
    for (const auto& block : Logger::s_prefixStack)
        fullPrefix << L"[" << block << L"] ";
    return fullPrefix.str();
}

std::string Logger::buildPrefixString() { return StringUtil::utf16toUTF8(buildPrefixWString()); }

bool Logger::shouldLog(const std::wstring& message)
{
    {
        const std::shared_lock lock(s_existingMessagesMutex);
        if (s_existingMessages.contains(message)) {
            // Don't log anything if already logged.
            return false;
        }
    }

    const std::unique_lock lock(s_existingMessagesMutex);
    const auto [_, inserted] = s_existingMessages.insert(message);
    return inserted; // true only for the first thread
}

void Logger::log(spdlog::level::level_enum level,
                 const std::wstring& message)
{
    const std::shared_lock lock(s_mtLogLock);

    if (level >= spdlog::level::warn) {
        if (s_threadMessageCapture)
            s_threadMessageCapture(level, message);

        if (!shouldLog(message))
            return;
    }

    if (s_isThreadedBufferActive) {
        s_curBuffer.emplace_back(level, message);
        return;
    }

    spdlog::log(level, L"{}", message);
}

void Logger::log(spdlog::level::level_enum level,
                 const std::string& message)
{
    const std::shared_lock lock(s_mtLogLock);

    if (level >= spdlog::level::warn) {
        const auto wideMessage = StringUtil::utf8toUTF16(message);
        if (s_threadMessageCapture)
            s_threadMessageCapture(level, wideMessage);

        if (!shouldLog(wideMessage))
            return;
    }

    if (s_isThreadedBufferActive) {
        s_curBuffer.emplace_back(level, message);
        return;
    }

    spdlog::log(level, "{}", message);
}

// ScopedPrefix class implementation.
Logger::Prefix::Prefix(const std::wstring& prefix)
{
    // Add the new prefix block to the stack.
    s_prefixStack.push_back(prefix);
}

Logger::Prefix::Prefix(const std::string& prefix)
{
    // Add the new prefix block to the stack.
    s_prefixStack.push_back(StringUtil::utf8toUTF16(prefix));
}

Logger::Prefix::~Prefix()
{
    // Remove the last prefix block.
    if (!s_prefixStack.empty())
        s_prefixStack.pop_back();
}

void Logger::setThreadMessageCapture(MessageCaptureFn captureFn) { s_threadMessageCapture = captureFn; }

void Logger::markRunStart()
{
    const std::unique_lock lock(s_existingMessagesMutex);
    s_runStartMessages = s_existingMessages;
    s_isRunStartMarked = true;
}

void Logger::resetToRunStart()
{
    const std::unique_lock lock(s_existingMessagesMutex);
    if (!s_isRunStartMarked)
        return;

    s_existingMessages = s_runStartMessages;
}

void Logger::startThreadedBuffer()
{
    s_isThreadedBufferActive = true;
    s_curBuffer.clear();
}

void Logger::flushThreadedBuffer()
{
    // Prevent any other log messages for the whole flush.
    const std::unique_lock lock(s_mtLogLock);

    s_isThreadedBufferActive = false;
    for (const auto& [level, message] : s_curBuffer) {
        std::visit(
            [level](auto&& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, std::wstring>)
                    spdlog::log(level, L"{}", value);
                else
                    spdlog::log(level, "{}", value);
            },
            message);
    }
    s_curBuffer.clear();
}
