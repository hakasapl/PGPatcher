#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

/**
 * @brief Utility functions for common file I/O operations.
 */
namespace FileUtil {

/**
 * @brief One entry of a directory listing.
 *
 * An entry is filled from the listing alone, so listing a directory costs no file system call per entry. Reparse points
 * (symbolic links, junctions and the like) are the exception: they are described by their target, the way
 * std::filesystem::directory_entry describes them, and listDirectory() queries the file system for each of them.
 */
struct DirectoryEntry {
    std::wstring name; /**< Name of the entry without the directory */
    bool isDirectory = false;
    bool isRegularFile = false;
    bool isHidden = false; /**< Has the hidden file attribute */
    bool isLink = false; /**< Symbolic link or junction */
    int64_t mtime = 0; /**< Last write time (file_time_type ticks) */
    uint64_t size = 0; /**< Size in bytes, 0 for directories */
};

/**
 * @brief Visitor of walkDirectory(). Receives the path of an entry relative to the walked directory and the entry.
 *
 * The contents of a directory are only visited if the visitor returns true for it.
 */
using DirectoryVisitor = std::function<bool(const std::wstring&, const DirectoryEntry&)>;

/**
 * @brief Lists the entries of a directory.
 *
 * @param directory Directory to list.
 * @param shouldSkipPermissionDenied Return an empty list instead of throwing if access to the directory is denied.
 * @return The entries of the directory, without "." and "..".
 * @throws std::filesystem::filesystem_error if the directory cannot be listed.
 */
std::vector<DirectoryEntry> listDirectory(const std::filesystem::path& directory,
                                          const bool& shouldSkipPermissionDenied = false);

/**
 * @brief Visits every entry below a directory.
 *
 * Behaves like std::filesystem::recursive_directory_iterator with skip_permission_denied: directories that cannot be
 * read are skipped, and symbolic links and junctions to directories are visited but not followed.
 *
 * @param root Directory to walk.
 * @param visitor Called for every entry, see DirectoryVisitor.
 * @throws std::filesystem::filesystem_error if a directory cannot be listed.
 */
void walkDirectory(const std::filesystem::path& root,
                   const DirectoryVisitor& visitor);

/**
 * @brief Reads the entire contents of a file into a byte vector.
 *
 * @param filePath Path to the file to read.
 * @return A vector of bytes containing the file contents, or an empty vector on failure.
 */
std::vector<std::byte> fileBytes(const std::filesystem::path& filePath);

/**
 * @brief Parses a JSON file from disk into a nlohmann::json object.
 *
 * @param filePath Path to the JSON file.
 * @param json Output parameter populated with the parsed JSON on success.
 * @return true if the file was opened and parsed successfully, false otherwise.
 */
bool getJSON(const std::filesystem::path& filePath,
             nlohmann::json& json);

/**
 * @brief Parses JSON from a byte vector into a nlohmann::json object.
 *
 * @param bytes Raw bytes containing a UTF-8 encoded JSON string.
 * @param json Output parameter populated with the parsed JSON on success.
 * @return true if parsing succeeded, false otherwise.
 */
bool getJSONFromBytes(const std::vector<std::byte>& bytes,
                      nlohmann::json& json);

/**
 * @brief Serializes a nlohmann::json object and writes it to a file.
 *
 * @param filePath Destination file path to write the JSON to.
 * @param json The JSON object to serialize.
 * @param readable If true, the output is pretty-printed with 2-space indentation; otherwise compact.
 * @return true if the file was written successfully, false otherwise.
 */
bool saveJSON(const std::filesystem::path& filePath,
              const nlohmann::json& json,
              const bool& shouldBeReadable);

}
