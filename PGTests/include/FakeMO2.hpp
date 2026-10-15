#pragma once

#include "TestUtil.hpp"

#include <filesystem>
#include <string>

namespace TestUtil {

/**
 * @brief A Mod Organizer 2 instance folder with a modorganizer.ini, a profile, a mods folder and an overwrite folder.
 */
class FakeMO2 {
private:
    TempDir m_dir;

public:
    FakeMO2()
    {
        std::filesystem::create_directories(instanceDir());
        std::filesystem::create_directories(modsDir());
        std::filesystem::create_directories(profileDir());
        writeINI("[General]\n"
                 "gameName=Skyrim Special Edition\n"
                 "game_edition=Steam\n"
                 "selected_profile=@ByteArray(Default)\n"
                 "gamePath=@ByteArray(C:\\\\Games\\\\Skyrim Special Edition)\n"
                 "\n"
                 "[Settings]\n"
                 "profiles_directory=@ByteArray(%BASE_DIR%/profiles)\n"
                 "mod_directory=@ByteArray(%BASE_DIR%/mods)\n");
    }

    [[nodiscard]] std::filesystem::path instanceDir() const { return m_dir / L"MO2"; }
    [[nodiscard]] std::filesystem::path modsDir() const { return instanceDir() / L"mods"; }
    [[nodiscard]] std::filesystem::path overwriteDir() const { return instanceDir() / L"overwrite"; }
    [[nodiscard]] std::filesystem::path profileDir() const { return instanceDir() / L"profiles\\Default"; }
    [[nodiscard]] std::filesystem::path outputDir() const { return m_dir / L"Output"; }

    /// @brief A folder for a game's data folder, next to the instance.
    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Game\\Data"; }

    void writeINI(const std::string& content) const
    {
        TestUtil::writeFile(instanceDir() / L"modorganizer.ini", content);
    }

    void writeModlist(const std::string& content) const { TestUtil::writeFile(profileDir() / L"modlist.txt", content); }

    void addModFile(const std::wstring& mod,
                    const std::wstring& relPath,
                    const std::string& content = "x") const
    {
        TestUtil::writeFile(modsDir() / mod / relPath, content);
    }

    void addOverwriteFile(const std::wstring& relPath,
                          const std::string& content = "x") const
    {
        TestUtil::writeFile(overwriteDir() / relPath, content);
    }

    void addDataFile(const std::wstring& relPath,
                     const std::string& content = "x") const
    {
        TestUtil::writeFile(dataDir() / relPath, content);
    }

    void makePortable() const { TestUtil::writeFile(instanceDir() / L"ModOrganizer.exe", "exe"); }
};

}
