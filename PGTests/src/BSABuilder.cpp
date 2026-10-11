#include "BSABuilder.hpp"

#include <bsa/tes4.hpp>

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TestUtil {

void BSABuilder::addFile(const std::string& relPath,
                         const std::vector<std::byte>& content)
{
    const auto separator = relPath.find_last_of("\\/");
    if (separator == std::string::npos)
        throw std::invalid_argument("archived files need a folder: " + relPath);

    m_files[relPath.substr(0, separator)].emplace_back(relPath.substr(separator + 1), content);
}

void BSABuilder::addFile(const std::string& relPath,
                         const std::string& content)
{
    std::vector<std::byte> bytes;
    bytes.reserve(content.size());
    for (const char ch : content)
        bytes.push_back(static_cast<std::byte>(ch));
    addFile(relPath, bytes);
}

void BSABuilder::write(const std::filesystem::path& path) const
{
    bsa::tes4::archive archive;
    archive.archive_flags(bsa::tes4::archive_flag::directory_strings | bsa::tes4::archive_flag::file_strings);
    archive.archive_types(bsa::tes4::archive_type::meshes | bsa::tes4::archive_type::textures
                          | bsa::tes4::archive_type::misc);

    for (const auto& [folder, files] : m_files) {
        bsa::tes4::directory directory;
        for (const auto& [name, content] : files) {
            bsa::tes4::file file;
            file.set_data(content);
            directory.insert(bsa::tes4::file::key(name), std::move(file));
        }
        archive.insert(bsa::tes4::directory::key(folder), std::move(directory));
    }

    std::filesystem::create_directories(path.parent_path());
    archive.write(path, bsa::tes4::version::sse);
}

}
