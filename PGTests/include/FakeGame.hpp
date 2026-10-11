#pragma once

#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace TestUtil {

/**
 * @brief A throwaway game install: a game folder with a Data folder and the game's master, a local AppData folder with
 * plugins.txt and a documents folder for the INI files, all below one temp directory.
 */
class FakeGame {
private:
    TempDir m_dir;
    BethesdaGame::GameType m_type;

public:
    /// @brief Creates the folders, the master plugin and an empty plugins.txt for the game type.
    explicit FakeGame(BethesdaGame::GameType type = BethesdaGame::GameType::SkyrimSE);

    [[nodiscard]] BethesdaGame::GameType type() const;
    [[nodiscard]] std::filesystem::path gameDir() const;
    [[nodiscard]] std::filesystem::path dataDir() const;
    [[nodiscard]] std::filesystem::path appDataDir() const;
    [[nodiscard]] std::filesystem::path documentsDir() const;
    [[nodiscard]] std::filesystem::path pluginsFile() const;

    /// @brief Name of the master plugin the game needs (e.g. Skyrim.esm).
    [[nodiscard]] std::wstring_view masterName() const;

    /**
     * @brief Writes a plugin into the Data folder and, when active, lists it in plugins.txt.
     *
     * @param name Plugin file name
     * @param isActive Whether plugins.txt lists it with a '*'
     * @param content File content, a placeholder by default
     */
    void addPlugin(const std::wstring& name,
                   bool isActive = true,
                   const std::vector<std::byte>& content = { }) const;

    /// @brief Appends a line to plugins.txt as is.
    void appendPluginsLine(const std::string& line) const;

    /// @brief Writes the game's main INI (skyrim.ini or enderal.ini) in the documents folder.
    void writeMainINI(const std::string& content);

    /// @brief Writes the game's custom INI in the documents folder.
    void writeCustomINI(const std::string& content);

    /// @brief Writes a file below the Data folder.
    void writeDataFile(const std::filesystem::path& relPath,
                       const std::string& content) const;

    /// @brief Writes a binary file below the Data folder.
    void writeDataFile(const std::filesystem::path& relPath,
                       const std::vector<std::byte>& content) const;

    /// @brief A BethesdaGame for this install (the game folder is validated on construction).
    [[nodiscard]] BethesdaGame game() const;
};

}
