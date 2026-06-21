#include "common/BethesdaGame.hpp"

#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <combaseapi.h>
#include <guiddef.h>
#include <knownfolders.h>
#include <minwindef.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlobj_core.h>
#include <winerror.h>
#include <winnt.h>
#include <winreg.h>
#else
#include <cstdlib>
#include <sstream>
#endif

using namespace std;

BethesdaGame::BethesdaGame(GameType gameType,
                           const filesystem::path& gamePath,
                           const filesystem::path& appDataPath,
                           const filesystem::path& documentPath)
    : m_objGameType(gameType)
{
    if (gamePath.empty()) {
        // If the game path is empty, find
        this->m_gamePath = findGamePathFromSteam(gameType);
    } else {
        // If the game path is not empty, use the provided game path
        this->m_gamePath = gamePath;
    }

    if (this->m_gamePath.empty()) {
        // If the game path is still empty, throw an exception
        throw runtime_error("Game path not found");
    }

    // check if path actually exists
    if (!filesystem::exists(this->m_gamePath)) {
        // If the game path does not exist, throw an exception
        throw runtime_error("Game path does not exist");
    }

    this->m_gameDataPath = this->m_gamePath / "Data";

    if (!isGamePathValid(this->m_gamePath, gameType)) {
        // If the game path does not contain Skyrim.esm, throw an exception
        throw runtime_error("Game data path does not contain Skyrim.esm");
    }

    // Define appdata path
    if (appDataPath.empty()) {
        m_gameAppDataPath = getGameAppdataSystemPath(m_objGameType);
    } else {
        m_gameAppDataPath = appDataPath;
    }

    // Define document path
    if (documentPath.empty()) {
        m_gameDocumentPath = getGameDocumentSystemPath();
    } else {
        m_gameDocumentPath = documentPath;
    }
}

auto BethesdaGame::isGamePathValid(const std::filesystem::path& gamePath,
                                   const GameType& type) -> bool
{
    // Check if the game path is valid
    const auto gameDataPath = gamePath / "Data";
    if (!filesystem::exists(gameDataPath) || !filesystem::is_directory(gameDataPath)) {
        return false;
    }

    const auto checkPath = gameDataPath / getDataCheckFile(type);
    if (!filesystem::exists(checkPath)) {
        return false;
    }

    // Check if plugins.txt exists for this game type
    const filesystem::path pluginsFile = getGameAppdataSystemPath(type) / "plugins.txt";
    return filesystem::exists(pluginsFile);
}

// Statics
auto BethesdaGame::getINILocations() const -> ININame
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return ININame {.ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini"};
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_GOG) {
        return ININame {.ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini"};
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return ININame {.ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini"};
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return ININame {.ini = "enderal.ini", .iniPrefs = "enderalprefs.ini", .iniCustom = "enderalcustom.ini"};
    }

    return {};
}

auto BethesdaGame::getDocumentLocation() const -> filesystem::path
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return "My Games/Skyrim Special Edition";
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_GOG) {
        return "My Games/Skyrim Special Edition GOG";
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return "My Games/Skyrim VR";
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return "My Games/Enderal Special Edition";
    }

    return {};
}

auto BethesdaGame::getAppDataLocation(const GameType& type) -> filesystem::path
{
    if (type == BethesdaGame::GameType::SKYRIM_SE) {
        return "Skyrim Special Edition";
    }

    if (type == BethesdaGame::GameType::SKYRIM_GOG) {
        return "Skyrim Special Edition GOG";
    }

    if (type == BethesdaGame::GameType::SKYRIM_VR) {
        return "Skyrim VR";
    }

    if (type == BethesdaGame::GameType::ENDERAL_SE) {
        return "Enderal Special Edition";
    }

    return {};
}

auto BethesdaGame::getSteamGameID() const -> int
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return static_cast<int>(SteamGameID::STEAMGAMEID_SKYRIM_SE);
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return static_cast<int>(SteamGameID::STEAMGAMEID_SKYRIM_VR);
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return static_cast<int>(SteamGameID::STEAMGAMEID_ENDERAL_SE);
    }

    return {};
}

auto BethesdaGame::getDataCheckFile(const GameType& type) -> filesystem::path
{
    if (type == BethesdaGame::GameType::SKYRIM_SE) {
        return "Skyrim.esm";
    }

    if (type == BethesdaGame::GameType::SKYRIM_GOG) {
        return "Skyrim.esm";
    }

    if (type == BethesdaGame::GameType::SKYRIM_VR) {
        return "SkyrimVR.esm";
    }

    if (type == BethesdaGame::GameType::ENDERAL_SE) {
        return "Enderal - Forgotten Stories.esm";
    }

    return {};
}

auto BethesdaGame::getGameType() const -> BethesdaGame::GameType { return m_objGameType; }

auto BethesdaGame::getGamePath() const -> filesystem::path
{
    // Get the game path from the registry
    // If the game is not found, return an empty string
    return m_gamePath;
}

auto BethesdaGame::getGameRegistryPath(const GameType& type) -> std::string
{
    const string basePathSkyrim = R"(SOFTWARE\WOW6432Node\bethesda softworks\)";
    const string basePathEnderal = R"(Software\Sure AI\)";

    const std::map<GameType, string> gameToPathMap {{GameType::SKYRIM_SE, basePathSkyrim + "Skyrim Special Edition"},
                                                    {GameType::SKYRIM_VR, basePathSkyrim + "Skyrim VR"},
                                                    {GameType::ENDERAL_SE, basePathEnderal + "EnderalSE"}};

    if (gameToPathMap.contains(type)) {
        return gameToPathMap.at(type);
    }

    return {};
}

auto BethesdaGame::getGameDataPath() const -> filesystem::path { return m_gameDataPath; }

auto BethesdaGame::findGamePathFromSteam(const GameType& type) -> filesystem::path
{
#ifdef _WIN32
    // TODO UNICODE get file path as UNICODE

    // Find the game path from the registry
    // If the game is not found, return an empty string

    HKEY baseHKey = (type == GameType::ENDERAL_SE) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const string regPath = getGameRegistryPath(type);

    std::vector<char> data(REG_BUFFER_SIZE, '\0');
    DWORD dataSize = REG_BUFFER_SIZE;

    const LONG result
        = RegGetValueA(baseHKey, regPath.c_str(), "Installed Path", RRF_RT_REG_SZ, nullptr, data.data(), &dataSize);
    if (result == ERROR_SUCCESS) {
        return {data.data()};
    }

    return {};
#else
    // On Linux: parse ~/.steam/steam/steamapps/libraryfolders.vdf to find installed games

    // Map game type to Steam App ID
    static const std::map<GameType, int> steamAppIDMap {
        {GameType::SKYRIM_SE, static_cast<int>(SteamGameID::STEAMGAMEID_SKYRIM_SE)},
        {GameType::SKYRIM_VR, static_cast<int>(SteamGameID::STEAMGAMEID_SKYRIM_VR)},
        {GameType::ENDERAL_SE, static_cast<int>(SteamGameID::STEAMGAMEID_ENDERAL_SE)},
    };

    if (!steamAppIDMap.contains(type)) {
        return {};
    }

    const int steamAppID = steamAppIDMap.at(type);
    if (steamAppID == 0) {
        return {};
    }

    // Find Steam root paths
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return {};
    }

    vector<filesystem::path> steamLibraries;

    // Default steam install path
    filesystem::path steamRoot = filesystem::path(home) / ".steam" / "steam";
    if (!filesystem::exists(steamRoot)) {
        steamRoot = filesystem::path(home) / ".local" / "share" / "Steam";
    }

    // Parse libraryfolders.vdf
    const filesystem::path vdfPath = steamRoot / "steamapps" / "libraryfolders.vdf";
    if (filesystem::exists(vdfPath)) {
        ifstream vdfFile(vdfPath);
        string line;
        while (getline(vdfFile, line)) {
            // Look for "path" entries
            const auto pathPos = line.find("\"path\"");
            if (pathPos != string::npos) {
                const auto firstQuote = line.find('"', pathPos + 6);
                const auto secondQuote = line.find('"', firstQuote + 1);
                if (firstQuote != string::npos && secondQuote != string::npos) {
                    steamLibraries.emplace_back(line.substr(firstQuote + 1, secondQuote - firstQuote - 1));
                }
            }
        }
    }

    // Always check default library
    steamLibraries.insert(steamLibraries.begin(), steamRoot);

    const string appIDStr = to_string(steamAppID);
    for (const auto& lib : steamLibraries) {
        const filesystem::path acfPath = lib / "steamapps" / ("appmanifest_" + appIDStr + ".acf");
        if (!filesystem::exists(acfPath)) {
            continue;
        }

        // Parse installdir from manifest
        ifstream acf(acfPath);
        string acfLine;
        while (getline(acf, acfLine)) {
            const auto pos = acfLine.find("\"installdir\"");
            if (pos != string::npos) {
                const auto q1 = acfLine.find('"', pos + 12);
                const auto q2 = acfLine.find('"', q1 + 1);
                if (q1 != string::npos && q2 != string::npos) {
                    const string installDir = acfLine.substr(q1 + 1, q2 - q1 - 1);
                    const filesystem::path gamePath = lib / "steamapps" / "common" / installDir;
                    if (filesystem::exists(gamePath)) {
                        return gamePath;
                    }
                }
            }
        }
    }

    return {};
#endif
}

auto BethesdaGame::getINIPaths() const -> BethesdaGame::ININame
{
    BethesdaGame::ININame output = getINILocations();
    const filesystem::path gameDocsPath = getGameDocumentSystemPath();

    // normal ini file
    output.ini = m_gameDocumentPath / output.ini;
    output.iniPrefs = m_gameDocumentPath / output.iniPrefs;
    output.iniCustom = m_gameDocumentPath / output.iniCustom;

    return output;
}

auto BethesdaGame::getPluginsFile() const -> filesystem::path
{
    const filesystem::path gamePluginsFile = m_gameAppDataPath / "plugins.txt";
    return gamePluginsFile;
}

auto BethesdaGame::getActivePlugins(const bool& trimExtension,
                                    const bool& lowercase) const -> vector<wstring>
{
    vector<wstring> outputLO;

    // Add base plugins
    outputLO.emplace_back(L"Skyrim.esm");
    if (filesystem::exists(m_gameDataPath / "Update.esm")) {
        outputLO.emplace_back(L"Update.esm");
    }
    if (filesystem::exists(m_gameDataPath / "Dawnguard.esm")) {
        outputLO.emplace_back(L"Dawnguard.esm");
    }
    if (filesystem::exists(m_gameDataPath / "HearthFires.esm")) {
        outputLO.emplace_back(L"HearthFires.esm");
    }
    if (filesystem::exists(m_gameDataPath / "Dragonborn.esm")) {
        outputLO.emplace_back(L"Dragonborn.esm");
    }
    if (getGameType() == GameType::SKYRIM_VR && filesystem::exists(m_gameDataPath / "SkyrimVR.esm")) {
        outputLO.emplace_back(L"SkyrimVR.esm");
    }

    // Add cc plugins
    const filesystem::path creationClubFile = getGamePath() / "Skyrim.ccc";
    if (filesystem::exists(creationClubFile)) {
        ifstream creationClubFileHandle(creationClubFile, 1);
        if (creationClubFileHandle.is_open()) {
            string line;
            while (getline(creationClubFileHandle, line)) {
                if (line.empty() || line[0] == '#') {
                    continue;
                }

                if (!filesystem::exists(m_gameDataPath / line)) {
                    continue;
                }

                // check if already exists in outputLO
                if (std::ranges::find_if(
                        outputLO,
                        [&](const std::wstring& s) { return boost::iequals(s, StringUtil::utf8toUTF16(line)); })
                        == outputLO.end()
                    && filesystem::exists(m_gameDataPath / line)) {

                    outputLO.push_back(StringUtil::utf8toUTF16(line));
                }
            }
        }
    }

    // Get the plugins file
    const filesystem::path pluginsFile = getPluginsFile();
    ifstream pluginsFileHandle(pluginsFile, 1);
    if (!pluginsFileHandle.is_open()) {
        throw runtime_error("Unable to open plugins.txt file");
    }

    // loop through each line of plugins.txt
    string line;
    while (getline(pluginsFileHandle, line)) {
        // Ignore lines that start with '#', which are comment lines
        if (line.empty() || line[0] == '#') {
            continue;
        }

        if (line.starts_with("*")) {
            // this is an active plugin
            line = line.substr(1);

            if (std::ranges::find_if(
                    outputLO, [&](const std::wstring& s) { return boost::iequals(s, StringUtil::utf8toUTF16(line)); })
                    == outputLO.end()
                && filesystem::exists(m_gameDataPath / StringUtil::utf8toUTF16(line))) {

                outputLO.push_back(StringUtil::utf8toUTF16(line));
            }
        }
    }

    // close file handle
    pluginsFileHandle.close();

    if (trimExtension) {
        // Trim the extension from the plugin name
        for (wstring& plugin : outputLO) {
            const auto dotPos = plugin.find_last_of(L'.');
            if (dotPos != wstring::npos) {
                plugin = plugin.substr(0, dotPos);
            }
        }
    }

    if (lowercase) {
        // Convert the plugin name to lowercase
        for (wstring& plugin : outputLO) {
            boost::to_lower(plugin);
        }
    }

    return outputLO;
}

auto BethesdaGame::getGameDocumentSystemPath() const -> filesystem::path
{
#ifdef _WIN32
    filesystem::path docPath = getSystemPath(FOLDERID_Documents);
    if (docPath.empty()) {
        return {};
    }

    docPath /= getDocumentLocation();
    return docPath;
#else
    // On Linux: use ~/Documents or ~ as the documents directory
    const char* home = std::getenv("HOME");
    if (home == nullptr) {
        return {};
    }

    filesystem::path docsPath = filesystem::path(home) / "Documents";
    if (!filesystem::exists(docsPath)) {
        docsPath = filesystem::path(home);
    }

    return docsPath / getDocumentLocation();
#endif
}

auto BethesdaGame::getGameAppdataSystemPath(const GameType& type) -> filesystem::path
{
#ifdef _WIN32
    filesystem::path appDataPath = getSystemPath(FOLDERID_LocalAppData);
    if (appDataPath.empty()) {
        return {};
    }

    appDataPath /= getAppDataLocation(type);
    return appDataPath;
#else
    // On Linux: use XDG_DATA_HOME or ~/.local/share
    const char* xdgData = std::getenv("XDG_DATA_HOME");
    filesystem::path appDataPath;
    if (xdgData != nullptr && filesystem::exists(xdgData)) {
        appDataPath = filesystem::path(xdgData);
    } else {
        const char* home = std::getenv("HOME");
        if (home == nullptr) {
            return {};
        }
        appDataPath = filesystem::path(home) / ".local" / "share";
    }

    appDataPath /= getAppDataLocation(type);
    return appDataPath;
#endif
}

#ifdef _WIN32
auto BethesdaGame::getSystemPath(const GUID& folderID) -> filesystem::path
{
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(folderID, 0, nullptr, &path);
    if (SUCCEEDED(result)) {
        wstring outPath(path);
        CoTaskMemFree(path); // Free the memory allocated for the path

        return outPath;
    }

    // Handle error
    return {};
}
#endif

auto BethesdaGame::getGameTypes() -> vector<GameType>
{
    const static auto gameTypes
        = vector<GameType> {GameType::SKYRIM_SE, GameType::SKYRIM_GOG, GameType::SKYRIM_VR, GameType::ENDERAL_SE};
    return gameTypes;
}

auto BethesdaGame::getStrFromGameType(const GameType& type) -> string
{
    const static auto gameTypeToStrMap = unordered_map<GameType, string> {{GameType::SKYRIM_SE, "Skyrim SE"},
                                                                          {GameType::SKYRIM_GOG, "Skyrim GOG"},
                                                                          {GameType::SKYRIM_VR, "Skyrim VR"},
                                                                          {GameType::ENDERAL_SE, "Enderal SE"}};

    if (gameTypeToStrMap.contains(type)) {
        return gameTypeToStrMap.at(type);
    }

    return gameTypeToStrMap.at(GameType::SKYRIM_SE);
}

auto BethesdaGame::getGameTypeFromStr(const string& type) -> GameType
{
    const static auto strToGameTypeMap = unordered_map<string, GameType> {{"Skyrim SE", GameType::SKYRIM_SE},
                                                                          {"Skyrim GOG", GameType::SKYRIM_GOG},
                                                                          {"Skyrim VR", GameType::SKYRIM_VR},
                                                                          {"Enderal SE", GameType::ENDERAL_SE}};

    if (strToGameTypeMap.contains(type)) {
        return strToGameTypeMap.at(type);
    }

    return strToGameTypeMap.at("Skyrim SE");
}
