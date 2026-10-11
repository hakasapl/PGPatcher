#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace TestUtil {

/**
 * @brief Builds a Skyrim SE BSA archive from in-memory files.
 */
class BSABuilder {
private:
    /// Files by folder: folder path -> (file name, content).
    std::map<std::string, std::vector<std::pair<std::string, std::vector<std::byte>>>> m_files;

public:
    /// @brief Adds a file. The path is relative to the data folder with backslashes, e.g. "meshes\\armor\\iron.nif".
    void addFile(const std::string& relPath,
                 const std::vector<std::byte>& content);

    /// @brief Adds a text file.
    void addFile(const std::string& relPath,
                 const std::string& content);

    /// @brief Writes the archive (uncompressed, with folder and file names) and creates its folders.
    void write(const std::filesystem::path& path) const;
};

}
