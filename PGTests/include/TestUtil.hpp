#pragma once

#include <spdlog/common.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Helpers shared by the unit tests.
 */
namespace TestUtil {

/**
 * @brief A unique directory below the system temp folder that is deleted when the object goes out of scope.
 */
class TempDir {
private:
    std::filesystem::path m_path;

public:
    TempDir();
    ~TempDir();

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const;

    /// @brief Path of an entry inside the directory.
    [[nodiscard]] std::filesystem::path operator/(const std::filesystem::path& rel) const;
};

/// @brief Writes a text file, creating its parent folders and overwriting an existing file.
void writeFile(const std::filesystem::path& path,
               const std::string& content);

/// @brief Writes a binary file, creating its parent folders and overwriting an existing file.
void writeFile(const std::filesystem::path& path,
               const std::vector<std::byte>& content);

/// @brief Reads a whole file as text.
std::string readFile(const std::filesystem::path& path);

/// @brief Creates a directory junction with mklink. Returns false when the link could not be created.
bool createJunction(const std::filesystem::path& link,
                    const std::filesystem::path& target);

/// @brief A message no other test logs, so Logger's duplicate suppression never hides it.
std::string uniqueMessage(const std::string& prefix);

/// @brief Wide string variant of uniqueMessage().
std::wstring uniqueMessageW(const std::wstring& prefix);

/// @brief UTF-8 form of a path, for assertion messages.
std::string pathToUTF8(const std::filesystem::path& path);

/// @brief Folder of the test executable, which is the build's bin folder.
std::filesystem::path executableDir();

/// @brief Waits until the predicate returns true or the timeout passes. Returns the predicate's last value.
bool waitFor(const std::function<bool()>& predicate,
             int timeoutMs = 5000);

/**
 * @brief Captures everything PGLib logs through spdlog, so tests can assert on it.
 */
namespace TestLog {
struct Entry {
    spdlog::level::level_enum level { };
    std::string message;
};

/// @brief Replaces the default spdlog logger with the capturing one. Called once from main().
void install();

/// @brief Every message captured since the last clear(), oldest first.
std::vector<Entry> entries();

/// @brief Forgets every captured message.
void clear();

/// @brief Whether a message of the given level containing needle was captured.
bool contains(spdlog::level::level_enum level,
              const std::string& needle);

/// @brief Number of captured messages of the given level containing needle.
size_t count(spdlog::level::level_enum level,
             const std::string& needle);
}

}
