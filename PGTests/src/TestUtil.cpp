#include "TestUtil.hpp"

#include "util/StringUtil.hpp"

#include <spdlog/details/log_msg.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/spdlog.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>
#include <windows.h>

namespace TestUtil {

namespace {
std::atomic<unsigned>& tempDirCounter()
{
    static std::atomic<unsigned> counter { 0 };
    return counter;
}

std::atomic<unsigned>& uniqueMessageCounter()
{
    static std::atomic<unsigned> counter { 0 };
    return counter;
}

/**
 * @brief spdlog sink that keeps every message in memory.
 */
class CaptureSink : public spdlog::sinks::base_sink<std::mutex> {
private:
    std::vector<TestLog::Entry> m_entries;

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        m_entries.push_back({ .level = msg.level, .message = std::string(msg.payload.data(), msg.payload.size()) });
    }

    void flush_() override { }

public:
    std::vector<TestLog::Entry> entries()
    {
        const std::scoped_lock lock(mutex_);
        return m_entries;
    }

    void clear()
    {
        const std::scoped_lock lock(mutex_);
        m_entries.clear();
    }
};

std::shared_ptr<CaptureSink>& captureSink()
{
    static std::shared_ptr<CaptureSink> sink;
    return sink;
}
}

TempDir::TempDir()
{
    const auto base = std::filesystem::temp_directory_path() / L"pgtests";
    const auto name = std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(tempDirCounter()++);
    m_path = base / name;
    std::filesystem::remove_all(m_path);
    std::filesystem::create_directories(m_path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
}

const std::filesystem::path& TempDir::path() const { return m_path; }

std::filesystem::path TempDir::operator/(const std::filesystem::path& rel) const { return m_path / rel; }

void writeFile(const std::filesystem::path& path,
               const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void writeFile(const std::filesystem::path& path,
               const std::vector<std::byte>& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(content.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
              static_cast<std::streamsize>(content.size()));
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

bool createJunction(const std::filesystem::path& link,
                    const std::filesystem::path& target)
{
    const std::wstring command
        = L"cmd /C mklink /J \"" + link.wstring() + L"\" \"" + target.wstring() + L"\" > nul 2>&1";
    return (_wsystem(command.c_str()) == 0) && std::filesystem::exists(link);
}

std::string uniqueMessage(const std::string& prefix)
{
    return prefix + " #" + std::to_string(GetCurrentProcessId()) + "." + std::to_string(uniqueMessageCounter()++);
}

std::wstring uniqueMessageW(const std::wstring& prefix)
{
    return StringUtil::utf8toUTF16(uniqueMessage(StringUtil::utf16toUTF8(prefix)));
}

std::string pathToUTF8(const std::filesystem::path& path) { return StringUtil::utf16toUTF8(path.wstring()); }

std::filesystem::path executableDir()
{
    std::array<wchar_t, MAX_PATH> buffer { };
    if (!GetModuleFileNameW(nullptr, buffer.data(), MAX_PATH))
        return { };

    return std::filesystem::path(buffer.data()).parent_path();
}

bool waitFor(const std::function<bool()>& predicate,
             int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!predicate()) {
        if (std::chrono::steady_clock::now() > deadline)
            return predicate();

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return true;
}

namespace TestLog {
void install()
{
    captureSink() = std::make_shared<CaptureSink>();
    const auto logger = std::make_shared<spdlog::logger>("pgtests", captureSink());
    logger->set_level(spdlog::level::trace);
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
}

std::vector<Entry> entries() { return captureSink() ? captureSink()->entries() : std::vector<Entry> { }; }

void clear()
{
    if (captureSink())
        captureSink()->clear();
}

bool contains(spdlog::level::level_enum level,
              const std::string& needle)
{
    return count(level, needle) > 0;
}

size_t count(spdlog::level::level_enum level,
             const std::string& needle)
{
    size_t num = 0;
    for (const auto& entry : entries())
        if (entry.level == level && entry.message.find(needle) != std::string::npos)
            num++;

    return num;
}
}

}
