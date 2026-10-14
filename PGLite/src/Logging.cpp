#include "PCH.h"

#include "Logging.hpp"

#include <spdlog/common.h>
#include <spdlog/details/log_msg.h>
#include <spdlog/logger.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/**
 * @brief Writes the formatted messages to a file opened by wide path, which the stock file sink cannot do without the
 * wide file name option of spdlog.
 */
class FileSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    explicit FileSink(const std::filesystem::path& path)
        : m_file(path,
                 std::ios::binary | std::ios::trunc)
    {
    }

    [[nodiscard]] bool isOpen() const { return m_file.is_open(); }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override // NOLINT(readability-identifier-naming)
    {
        spdlog::memory_buf_t formatted;
        formatter_->format(message, formatted);
        m_file.write(formatted.data(), static_cast<std::streamsize>(formatted.size()));
    }

    void flush_() override { m_file.flush(); } // NOLINT(readability-identifier-naming)

private:
    std::ofstream m_file;
};

/**
 * @brief Keeps every message of warning level and above for the GUI, each once, the newest maxMessages of them.
 */
class MessageSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    static constexpr std::size_t maxMessages = 2000;

    [[nodiscard]] std::vector<Logging::Message> messages()
    {
        const std::scoped_lock lock(mutex_);
        return m_messages;
    }

    void clear()
    {
        const std::scoped_lock lock(mutex_);
        m_messages.clear();
        m_seen.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& message) override // NOLINT(readability-identifier-naming)
    {
        // The base sink holds the mutex while this runs.
        if (message.level < spdlog::level::warn)
            return;

        std::string text(message.payload.data(), message.payload.size());
        if (!m_seen.insert(text).second)
            return;

        if (m_messages.size() >= maxMessages) {
            // The oldest message goes, and is kept again if it is logged again.
            m_seen.erase(m_messages.front().text);
            m_messages.erase(m_messages.begin());
        }
        m_messages.push_back({ .level = message.level, .text = std::move(text) });
    }

    void flush_() override { } // NOLINT(readability-identifier-naming)

private:
    std::vector<Logging::Message> m_messages;
    std::unordered_set<std::string> m_seen;
};

std::shared_ptr<MessageSink> s_messageSink;

}

bool Logging::initialize(spdlog::level::level_enum level)
{
    const auto folder = SKSE::log::log_directory();
    if (!folder)
        return false;

    const auto fileSink = std::make_shared<FileSink>(*folder / L"PGLite.log");
    if (!fileSink->isOpen())
        return false;

    s_messageSink = std::make_shared<MessageSink>();

    std::vector<spdlog::sink_ptr> sinks { fileSink, s_messageSink };
    const auto logger = std::make_shared<spdlog::logger>("PGLite", sinks.begin(), sinks.end());
    spdlog::set_default_logger(logger);
    spdlog::set_level(level);
    spdlog::flush_on(level);
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

    return true;
}

std::vector<Logging::Message> Logging::messages()
{
    if (!s_messageSink)
        return { };

    return s_messageSink->messages();
}

void Logging::clearMessages()
{
    if (s_messageSink)
        s_messageSink->clear();
}

spdlog::level::level_enum Logging::levelFromString(const std::string& level)
{
    if (level == "trace")
        return spdlog::level::trace;
    if (level == "debug")
        return spdlog::level::debug;
    if (level == "warn")
        return spdlog::level::warn;
    if (level == "error")
        return spdlog::level::err;

    return spdlog::level::info;
}

}
