#include "util/FileUtil.hpp"

#include <nlohmann/detail/output/serializer.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <windows.h>

namespace FileUtil {
std::vector<std::byte> fileBytes(const std::filesystem::path& filePath)
{
    std::ifstream inputFile(filePath, std::ios::binary | std::ios::ate);
    if (!inputFile.is_open()) {
        // Unable to open file.
        return { };
    }

    const auto length = inputFile.tellg();
    if (length == -1) {
        // Unable to find length.
        inputFile.close();
        return { };
    }

    inputFile.seekg(0, std::ios::beg);

    // Make a buffer of the exact size of the file and read the data into it.
    std::vector<std::byte> buffer(length);
    inputFile.read(reinterpret_cast<char*>(buffer.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
                   length);

    inputFile.close();

    return buffer;
}

bool getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json)
{
    std::ifstream inputFile(filePath);
    if (!inputFile.is_open()) {
        // Unable to open file.
        return false;
    }

    try {
        inputFile >> json;
    } catch (...) {
        // Handle JSON parsing error.
        return false;
    }

    inputFile.close();
    return true;
}

bool getJSONFromBytes(const std::vector<std::byte>& bytes,
                      nlohmann::json& json)
{
    try {
        // Convert vector of bytes to string.
        std::string jsonString(bytes.size(), '\0');
        std::memcpy(jsonString.data(), bytes.data(), bytes.size());

        // Parse the JSON string.
        json = nlohmann::json::parse(jsonString);
    } catch (...) {
        // Handle JSON parsing error.
        return false;
    }

    return true;
}

bool saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& shouldBeReadable)
{
    std::ofstream outputFile;
    outputFile.exceptions(std::ios::failbit | std::ios::badbit);
    outputFile.open(filePath, std::ios::binary);
    if (!outputFile.is_open()) {
        // Unable to open file.
        return false;
    }

    if (shouldBeReadable)
        outputFile << json.dump(2, ' ', false, nlohmann::detail::error_handler_t::replace);
    else
        outputFile << json.dump(-1, ' ', false, nlohmann::detail::error_handler_t::replace);

    outputFile.close();
    return true;
}

std::vector<DirectoryEntry> listDirectory(const std::filesystem::path& directory,
                                          const bool& shouldSkipPermissionDenied)
{
    std::vector<DirectoryEntry> entries;

    // This is the call the std::filesystem iterators make. The basic info level leaves out the short (8.3) name.
    const std::filesystem::path pattern = directory / L"*";
    WIN32_FIND_DATAW findData { };
    HANDLE findHandle
        = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &findData, FindExSearchNameMatch, nullptr, 0);
    if (findHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED && shouldSkipPermissionDenied)
            return entries;

        if (error == ERROR_FILE_NOT_FOUND) {
            // A directory without any entry at all, which only the root of an empty volume is.
            std::error_code ec;
            if (std::filesystem::exists(directory, ec))
                return entries;
        }

        throw std::filesystem::filesystem_error(
            "Unable to list directory", directory, std::error_code(static_cast<int>(error), std::system_category()));
    }

    const std::unique_ptr<void, decltype(&FindClose)> findHandleCloser(findHandle, &FindClose);

    DWORD error = ERROR_SUCCESS;
    while (!error) {
        const std::wstring_view name = std::data(findData.cFileName);
        if (name != L"." && name != L"..") {
            DirectoryEntry entry;
            entry.name = name;
            entry.isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0U;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                // The listing describes the link itself. Type, size and write time are those of its target, which is
                // what std::filesystem::directory_entry reports as well.
                //
                // Symbolic links and junctions are the two tags std::filesystem::recursive_directory_iterator does not
                // descend into. It enters a directory behind any other reparse tag, and so does walkDirectory().
                entry.isLink = findData.dwReserved0 == IO_REPARSE_TAG_SYMLINK
                    || findData.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT;

                const std::filesystem::path path = directory / entry.name;
                std::error_code ec;
                const auto status = std::filesystem::status(path, ec);
                entry.isDirectory = std::filesystem::is_directory(status);
                entry.isRegularFile = std::filesystem::is_regular_file(status);

                ec.clear();
                entry.mtime = std::filesystem::last_write_time(path, ec).time_since_epoch().count();

                ec.clear();
                const auto size = entry.isDirectory ? 0 : std::filesystem::file_size(path, ec);
                entry.size = ec ? 0 : size;
            } else {
                entry.isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
                entry.isRegularFile = !entry.isDirectory;
                entry.mtime = (static_cast<int64_t>(findData.ftLastWriteTime.dwHighDateTime) << 32)
                    | static_cast<int64_t>(findData.ftLastWriteTime.dwLowDateTime);
                if (!entry.isDirectory) {
                    entry.size = (static_cast<uint64_t>(findData.nFileSizeHigh) << 32)
                        | static_cast<uint64_t>(findData.nFileSizeLow);
                }
            }

            entries.push_back(std::move(entry));
        }

        if (!FindNextFileW(findHandle, &findData))
            error = GetLastError();
    }

    if (error != ERROR_NO_MORE_FILES) {
        throw std::filesystem::filesystem_error(
            "Unable to list directory", directory, std::error_code(static_cast<int>(error), std::system_category()));
    }

    return entries;
}

void walkDirectory(const std::filesystem::path& root,
                   const DirectoryVisitor& visitor)
{
    // Directories that still have to be listed, each with its path relative to root (ending with a separator).
    std::vector<std::pair<std::filesystem::path, std::wstring>> pendingDirectories;
    pendingDirectories.emplace_back(root, std::wstring());

    while (!pendingDirectories.empty()) {
        const auto [directory, relPrefix] = std::move(pendingDirectories.back());
        pendingDirectories.pop_back();

        for (const auto& entry : listDirectory(directory, true)) {
            const std::wstring relPath = relPrefix + entry.name;
            const bool shouldEnter = visitor(relPath, entry);

            // Links are visited but never followed.
            if (shouldEnter && entry.isDirectory && !entry.isLink) {
                pendingDirectories.emplace_back(directory / entry.name,
                                                relPath + std::filesystem::path::preferred_separator);
            }
        }
    }
}
}
