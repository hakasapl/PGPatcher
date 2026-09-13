#include "common/BethesdaGame.hpp"

#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <combaseapi.h>
#include <filesystem>
#include <fstream>
#include <guiddef.h>
#include <knownfolders.h>
#include <map>
#include <minwindef.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlobj_core.h>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <winerror.h>
#include <winnt.h>
#include <winreg.h>

BethesdaGame::BethesdaGame(GameType gameType,
                           const std::filesystem::path& gamePath,
                           const std::filesystem::path& appDataPath,
                           const std::filesystem::path& documentPath)
    : m_objGameType(gameType)
{
    if (gamePath.empty()) {
        // If the game path is empty, find.
        this->m_gamePath = findGamePathFromSteam(gameType);
    } else {
        // If the game path is not empty, use the provided game path.
        this->m_gamePath = gamePath;
    }

    if (this->m_gamePath.empty()) {
        // If the game path is still empty, throw an exception.
        throw std::runtime_error("Game path not found");
    }

    // Check if path actually exists.
    if (!std::filesystem::exists(this->m_gamePath)) {
        // If the game path does not exist, throw an exception.
        throw std::runtime_error("Game path does not exist");
    }

    this->m_gameDataPath = this->m_gamePath / "Data";

    if (!isGamePathValid(this->m_gamePath, gameType)) {
        // If the game path does not contain Skyrim.esm, throw an exception.
        throw std::runtime_error("Game data path does not contain Skyrim.esm");
    }

    // Define appdata path.
    if (appDataPath.empty())
        m_gameAppDataPath = gameAppdataSystemPath(m_objGameType);
    else
        m_gameAppDataPath = appDataPath;

    // Define document path.
    if (documentPath.empty())
        m_gameDocumentPath = gameDocumentSystemPath();
    else
        m_gameDocumentPath = documentPath;
}

bool BethesdaGame::isGamePathValid(const std::filesystem::path& gamePath,
                                   const GameType& type)
{
    // Check if the game path is valid.
    const auto gameDataPath = gamePath / "Data";
    if (!std::filesystem::exists(gameDataPath) || !std::filesystem::is_directory(gameDataPath))
        return false;

    const auto checkPath = gameDataPath / dataCheckFile(type);
    if (!std::filesystem::exists(checkPath))
        return false;

    // Check if plugins.txt exists for this game type.
    const std::filesystem::path pluginsFile = gameAppdataSystemPath(type) / "plugins.txt";
    return std::filesystem::exists(pluginsFile);
}

// Statics.
auto BethesdaGame::iniLocations() const -> ININame
{
    if (m_objGameType == BethesdaGame::GameType::SkyrimSE)
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };

    if (m_objGameType == BethesdaGame::GameType::SkyrimGOG)
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };

    if (m_objGameType == BethesdaGame::GameType::SkyrimVR)
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };

    if (m_objGameType == BethesdaGame::GameType::EnderalSE)
        return ININame { .ini = "enderal.ini", .iniPrefs = "enderalprefs.ini", .iniCustom = "enderalcustom.ini" };

    return { };
}

std::filesystem::path BethesdaGame::documentLocation() const
{
    if (m_objGameType == BethesdaGame::GameType::SkyrimSE)
        return "My Games/Skyrim Special Edition";

    if (m_objGameType == BethesdaGame::GameType::SkyrimGOG)
        return "My Games/Skyrim Special Edition GOG";

    if (m_objGameType == BethesdaGame::GameType::SkyrimVR)
        return "My Games/Skyrim VR";

    if (m_objGameType == BethesdaGame::GameType::EnderalSE)
        return "My Games/Enderal Special Edition";

    return { };
}

std::filesystem::path BethesdaGame::appDataLocation(const GameType& type)
{
    if (type == BethesdaGame::GameType::SkyrimSE)
        return "Skyrim Special Edition";

    if (type == BethesdaGame::GameType::SkyrimGOG)
        return "Skyrim Special Edition GOG";

    if (type == BethesdaGame::GameType::SkyrimVR)
        return "Skyrim VR";

    if (type == BethesdaGame::GameType::EnderalSE)
        return "Enderal Special Edition";

    return { };
}

int BethesdaGame::steamGameID() const
{
    if (m_objGameType == BethesdaGame::GameType::SkyrimSE)
        return static_cast<int>(SteamGameID::SkyrimSE);

    if (m_objGameType == BethesdaGame::GameType::SkyrimVR)
        return static_cast<int>(SteamGameID::SkyrimVR);

    if (m_objGameType == BethesdaGame::GameType::EnderalSE)
        return static_cast<int>(SteamGameID::EnderalSE);

    return { };
}

std::filesystem::path BethesdaGame::dataCheckFile(const GameType& type)
{
    if (type == BethesdaGame::GameType::SkyrimSE)
        return "Skyrim.esm";

    if (type == BethesdaGame::GameType::SkyrimGOG)
        return "Skyrim.esm";

    if (type == BethesdaGame::GameType::SkyrimVR)
        return "SkyrimVR.esm";

    if (type == BethesdaGame::GameType::EnderalSE)
        return "Enderal - Forgotten Stories.esm";

    return { };
}

auto BethesdaGame::gameType() const -> BethesdaGame::GameType { return m_objGameType; }

std::filesystem::path BethesdaGame::gamePath() const
{
    // Get the game path from the registry.
    // If the game is not found, return an empty string.
    return m_gamePath;
}

std::string BethesdaGame::gameRegistryPath(const GameType& type)
{
    const std::string basePathSkyrim = R"(SOFTWARE\WOW6432Node\bethesda softworks\)";
    const std::string basePathEnderal = R"(Software\Sure AI\)";

    const std::map<GameType, std::string> gameToPathMap {
        { GameType::SkyrimSE, basePathSkyrim + "Skyrim Special Edition" },
        { GameType::SkyrimVR, basePathSkyrim + "Skyrim VR" },
        { GameType::EnderalSE, basePathEnderal + "EnderalSE" },
    };

    if (gameToPathMap.contains(type))
        return gameToPathMap.at(type);

    return { };
}

std::filesystem::path BethesdaGame::gameDataPath() const { return m_gameDataPath; }

std::filesystem::path BethesdaGame::findGamePathFromSteam(const GameType& type)
{
    // FIXME: Get the file path as UNICODE.

    // Find the game path from the registry.
    // If the game is not found, return an empty string.

    HKEY baseHKey = (type == GameType::EnderalSE) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const std::string regPath = gameRegistryPath(type);

    std::vector<char> data(regBufferSize, '\0');
    DWORD dataSize = regBufferSize;

    const LONG result
        = RegGetValueA(baseHKey, regPath.c_str(), "Installed Path", RRF_RT_REG_SZ, nullptr, data.data(), &dataSize);
    if (result == ERROR_SUCCESS)
        return { data.data() };

    return { };
}

auto BethesdaGame::iniPaths() const -> BethesdaGame::ININame
{
    BethesdaGame::ININame output = iniLocations();
    const std::filesystem::path gameDocsPath = gameDocumentSystemPath();

    // Normal ini file.
    output.ini = m_gameDocumentPath / output.ini;
    output.iniPrefs = m_gameDocumentPath / output.iniPrefs;
    output.iniCustom = m_gameDocumentPath / output.iniCustom;

    return output;
}

std::filesystem::path BethesdaGame::pluginsFile() const
{
    const std::filesystem::path gamePluginsFile = m_gameAppDataPath / "plugins.txt";
    return gamePluginsFile;
}

std::vector<std::wstring> BethesdaGame::activePlugins(const bool& shouldTrimExtension,
                                                      const bool& shouldLowercase) const
{
    std::vector<std::wstring> outputLO;

    // Add base plugins.
    outputLO.emplace_back(L"Skyrim.esm");
    if (std::filesystem::exists(m_gameDataPath / "Update.esm"))
        outputLO.emplace_back(L"Update.esm");
    if (std::filesystem::exists(m_gameDataPath / "Dawnguard.esm"))
        outputLO.emplace_back(L"Dawnguard.esm");
    if (std::filesystem::exists(m_gameDataPath / "HearthFires.esm"))
        outputLO.emplace_back(L"HearthFires.esm");
    if (std::filesystem::exists(m_gameDataPath / "Dragonborn.esm"))
        outputLO.emplace_back(L"Dragonborn.esm");
    if (gameType() == GameType::SkyrimVR && std::filesystem::exists(m_gameDataPath / "SkyrimVR.esm"))
        outputLO.emplace_back(L"SkyrimVR.esm");

    // Add cc plugins.
    const std::filesystem::path creationClubFile = gamePath() / "Skyrim.ccc";
    if (std::filesystem::exists(creationClubFile)) {
        std::ifstream creationClubFileHandle(creationClubFile, 1);
        if (creationClubFileHandle.is_open()) {
            std::string line;
            while (getline(creationClubFileHandle, line)) {
                if (line.empty() || line[0] == '#')
                    continue;

                if (!std::filesystem::exists(m_gameDataPath / line))
                    continue;

                // Check if already exists in outputLO.
                if (std::ranges::find_if(
                        outputLO,
                        [&](const std::wstring& s) { return boost::iequals(s, StringUtil::utf8toUTF16(line)); })
                        == outputLO.end()
                    && std::filesystem::exists(m_gameDataPath / line)) {

                    outputLO.push_back(StringUtil::utf8toUTF16(line));
                }
            }
        }
    }

    // Get the plugins file.
    const std::filesystem::path pluginsFile = this->pluginsFile();
    std::ifstream pluginsFileHandle(pluginsFile, 1);
    if (!pluginsFileHandle.is_open())
        throw std::runtime_error("Unable to open plugins.txt file");

    // Loop through each line of plugins.txt.
    std::string line;
    while (getline(pluginsFileHandle, line)) {
        // Ignore lines that start with '#', which are comment lines.
        if (line.empty() || line[0] == '#')
            continue;

        if (line.starts_with('*')) {
            // This is an active plugin.
            line = line.substr(1);

            if (std::ranges::find_if(
                    outputLO, [&](const std::wstring& s) { return boost::iequals(s, StringUtil::utf8toUTF16(line)); })
                    == outputLO.end()
                && std::filesystem::exists(m_gameDataPath / StringUtil::utf8toUTF16(line))) {

                outputLO.push_back(StringUtil::utf8toUTF16(line));
            }
        }
    }

    // Close file handle.
    pluginsFileHandle.close();

    if (shouldTrimExtension) {
        // Trim the extension from the plugin name.
        for (std::wstring& plugin : outputLO) {
            const auto dotPos = plugin.find_last_of(L'.');
            if (dotPos != std::wstring::npos)
                plugin = plugin.substr(0, dotPos);
        }
    }

    if (shouldLowercase) {
        // Convert the plugin name to lowercase.
        for (std::wstring& plugin : outputLO)
            boost::to_lower(plugin);
    }

    return outputLO;
}

std::filesystem::path BethesdaGame::gameDocumentSystemPath() const
{
    std::filesystem::path docPath = systemPath(FOLDERID_Documents);
    if (docPath.empty())
        return { };

    docPath /= documentLocation();
    return docPath;
}

std::filesystem::path BethesdaGame::gameAppdataSystemPath(const GameType& type)
{
    std::filesystem::path appDataPath = systemPath(FOLDERID_LocalAppData);
    if (appDataPath.empty())
        return { };

    appDataPath /= appDataLocation(type);
    return appDataPath;
}

std::filesystem::path BethesdaGame::systemPath(const GUID& folderID)
{
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folderID, 0, nullptr, &path);
    if (SUCCEEDED(result)) {
        std::wstring outPath(path);
        CoTaskMemFree(path); // Free the memory allocated for the path

        return outPath;
    }

    // Handle error.
    return { };
}

auto BethesdaGame::gameTypes() -> std::vector<GameType>
{
    const static auto gameTypes
        = std::vector<GameType> { GameType::SkyrimSE, GameType::SkyrimGOG, GameType::SkyrimVR, GameType::EnderalSE };
    return gameTypes;
}

std::string BethesdaGame::strFromGameType(const GameType& type)
{
    const static auto gameTypeToStrMap = std::unordered_map<GameType, std::string> {
        { GameType::SkyrimSE, "Skyrim SE" },
        { GameType::SkyrimGOG, "Skyrim GOG" },
        { GameType::SkyrimVR, "Skyrim VR" },
        { GameType::EnderalSE, "Enderal SE" },
    };

    if (gameTypeToStrMap.contains(type))
        return gameTypeToStrMap.at(type);

    return gameTypeToStrMap.at(GameType::SkyrimSE);
}

auto BethesdaGame::gameTypeFromStr(const std::string& type) -> GameType
{
    const static auto strToGameTypeMap = std::unordered_map<std::string, GameType> {
        { "Skyrim SE", GameType::SkyrimSE },
        { "Skyrim GOG", GameType::SkyrimGOG },
        { "Skyrim VR", GameType::SkyrimVR },
        { "Enderal SE", GameType::EnderalSE },
    };

    if (strToGameTypeMap.contains(type))
        return strToGameTypeMap.at(type);

    return strToGameTypeMap.at("Skyrim SE");
}
