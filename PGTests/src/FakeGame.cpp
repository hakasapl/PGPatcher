#include "FakeGame.hpp"

#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"
#include "util/StringUtil.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace TestUtil {

FakeGame::FakeGame(BethesdaGame::GameType type)
    : m_type(type)
{
    std::filesystem::create_directories(dataDir());
    std::filesystem::create_directories(appDataDir());
    std::filesystem::create_directories(documentsDir());
    writeFile(dataDir() / masterName(), "TES4");
    writeFile(pluginsFile(), "# This file is used by Skyrim to keep track of your downloaded content.\n");
}

BethesdaGame::GameType FakeGame::type() const { return m_type; }

std::filesystem::path FakeGame::gameDir() const { return m_dir / L"Game"; }

std::filesystem::path FakeGame::dataDir() const { return gameDir() / L"Data"; }

std::filesystem::path FakeGame::appDataDir() const { return m_dir / L"AppData"; }

std::filesystem::path FakeGame::documentsDir() const { return m_dir / L"Documents"; }

std::filesystem::path FakeGame::pluginsFile() const { return appDataDir() / L"plugins.txt"; }

std::wstring_view FakeGame::masterName() const
{
    switch (m_type) {
    case BethesdaGame::GameType::SkyrimVR:
        return L"SkyrimVR.esm";
    case BethesdaGame::GameType::EnderalSE:
        return L"Enderal - Forgotten Stories.esm";
    default:
        return L"Skyrim.esm";
    }
}

void FakeGame::addPlugin(const std::wstring& name,
                         bool isActive,
                         const std::vector<std::byte>& content) const
{
    if (content.empty())
        writeFile(dataDir() / name, "TES4");
    else
        writeFile(dataDir() / name, content);

    if (isActive)
        appendPluginsLine("*" + StringUtil::utf16toUTF8(name));
}

void FakeGame::appendPluginsLine(const std::string& line) const
{
    std::ofstream out(pluginsFile(), std::ios::app | std::ios::binary);
    out << line << "\n";
}

void FakeGame::writeMainINI(const std::string& content)
{
    const bool isEnderal = m_type == BethesdaGame::GameType::EnderalSE;
    writeFile(documentsDir() / (isEnderal ? L"enderal.ini" : L"skyrim.ini"), content);
}

void FakeGame::writeCustomINI(const std::string& content)
{
    const bool isEnderal = m_type == BethesdaGame::GameType::EnderalSE;
    writeFile(documentsDir() / (isEnderal ? L"enderalcustom.ini" : L"skyrimcustom.ini"), content);
}

void FakeGame::writeDataFile(const std::filesystem::path& relPath,
                             const std::string& content) const
{
    writeFile(dataDir() / relPath, content);
}

void FakeGame::writeDataFile(const std::filesystem::path& relPath,
                             const std::vector<std::byte>& content) const
{
    writeFile(dataDir() / relPath, content);
}

BethesdaGame FakeGame::game() const { return BethesdaGame(m_type, gameDir(), appDataDir(), documentsDir()); }

}
