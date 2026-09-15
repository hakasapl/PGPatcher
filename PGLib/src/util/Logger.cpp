#include "util/Logger.hpp"

#include "util/StringUtil.hpp"

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

// Static thread-local variables.
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
    s_runStartMarked = true;
}

void Logger::resetToRunStart()
{
    const std::unique_lock lock(s_existingMessagesMutex);
    if (!s_runStartMarked)
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
