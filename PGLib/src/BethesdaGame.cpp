#include "BethesdaGame.hpp"

#include "util/ParallaxGenUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <combaseapi.h>
#include <cstdlib>
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

using namespace std;

BethesdaGame::BethesdaGame(GameType gameType, const bool& logging, const filesystem::path& gamePath,
    const filesystem::path& appDataPath, const filesystem::path& documentPath)
    : m_objGameType(gameType)
    , m_logging(logging)
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
        if (this->m_logging) {
            spdlog::critical("Unable to locate game data path. Please specify the game data path "
                             "manually using the -d argument.");
            exit(1);
        } else {
            throw runtime_error("Game path not found");
        }
    }

    // check if path actually exists
    if (!filesystem::exists(this->m_gamePath)) {
        // If the game path does not exist, throw an exception
        if (this->m_logging) {
            spdlog::critical(L"Game path does not exist: {}", this->m_gamePath.wstring());
            exit(1);
        } else {
            throw runtime_error("Game path does not exist");
        }
    }

    this->m_gameDataPath = this->m_gamePath / "Data";

    if (!isGamePathValid(this->m_gamePath, gameType)) {
        // If the game path does not contain Skyrim.esm, throw an exception
        if (this->m_logging) {
            spdlog::critical(
                L"Game data location is invalid: {} does not contain Skyrim.esm", this->m_gameDataPath.wstring());
            exit(1);
        } else {
            throw runtime_error("Game data path does not contain Skyrim.esm");
        }
    }

    // Define appdata path
    if (appDataPath.empty()) {
        m_gameAppDataPath = getGameAppdataSystemPath();
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

auto BethesdaGame::isGamePathValid(const std::filesystem::path& gamePath, const GameType& type) -> bool
{
    // Check if the game path is valid
    const auto gameDataPath = gamePath / "Data";

    const auto checkPath = gameDataPath / getDataCheckFile(type);
    return filesystem::exists(checkPath);
}

// Statics
auto BethesdaGame::getINILocations() const -> ININame
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_GOG) {
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM) {
        return ININame { .ini = "skyrim.ini", .iniPrefs = "skyrimprefs.ini", .iniCustom = "skyrimcustom.ini" };
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL) {
        return ININame { .ini = "enderal.ini", .iniPrefs = "enderalprefs.ini", .iniCustom = "enderalcustom.ini" };
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return ININame { .ini = "enderal.ini", .iniPrefs = "enderalprefs.ini", .iniCustom = "enderalcustom.ini" };
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

    if (m_objGameType == BethesdaGame::GameType::SKYRIM) {
        return "My Games/Skyrim";
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL) {
        return "My Games/Enderal";
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return "My Games/Enderal Special Edition";
    }

    return {};
}

auto BethesdaGame::getAppDataLocation() const -> filesystem::path
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return "Skyrim Special Edition";
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_GOG) {
        return "Skyrim Special Edition GOG";
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return "Skyrim VR";
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM) {
        return "Skyrim";
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL) {
        return "Enderal";
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return "Enderal Special Edition";
    }

    return {};
}

auto BethesdaGame::getSteamGameID() const -> int
{
    if (m_objGameType == BethesdaGame::GameType::SKYRIM_SE) {
        return STEAMGAMEID_SKYRIM_SE;
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM_VR) {
        return STEAMGAMEID_SKYRIM_VR;
    }

    if (m_objGameType == BethesdaGame::GameType::SKYRIM) {
        return STEAMGAMEID_SKYRIM;
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL) {
        return STEAMGAMEID_ENDERAL;
    }

    if (m_objGameType == BethesdaGame::GameType::ENDERAL_SE) {
        return STEAMGAMEID_ENDERAL_SE;
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

    if (type == BethesdaGame::GameType::SKYRIM) {
        return "Skyrim.esm";
    }

    if (type == BethesdaGame::GameType::ENDERAL) {
        return "Enderal - Forgotten Stories.esm";
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

    const std::map<GameType, string> gameToPathMap { { GameType::SKYRIM_SE, basePathSkyrim + "Skyrim Special Edition" },
        { GameType::SKYRIM, basePathSkyrim + "Skyrim" }, { GameType::SKYRIM_VR, basePathSkyrim + "Skyrim VR" },
        { GameType::ENDERAL, basePathEnderal + "Enderal" }, { GameType::ENDERAL_SE, basePathEnderal + "EnderalSE" } };

    if (gameToPathMap.contains(type)) {
        return gameToPathMap.at(type);
    }

    return {};
}

auto BethesdaGame::getGameDataPath() const -> filesystem::path { return m_gameDataPath; }

auto BethesdaGame::findGamePathFromSteam(const GameType& type) -> filesystem::path
{
    // TODO UNICODE get file path as UNICODE

    // Find the game path from the registry
    // If the game is not found, return an empty string

    HKEY baseHKey
        = (type == GameType::ENDERAL || type == GameType::ENDERAL_SE) ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
    const string regPath = getGameRegistryPath(type);

    std::vector<char> data(REG_BUFFER_SIZE, '\0');
    DWORD dataSize = REG_BUFFER_SIZE;

    const LONG result
        = RegGetValueA(baseHKey, regPath.c_str(), "Installed Path", RRF_RT_REG_SZ, nullptr, data.data(), &dataSize);
    if (result == ERROR_SUCCESS) {
        return { data.data() };
    }

    return {};
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

auto BethesdaGame::getActivePlugins(const bool& trimExtension, const bool& lowercase) const -> vector<wstring>
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
                if (std::ranges::find_if(outputLO,
                        [&](const std::wstring& s) { return boost::iequals(s, ParallaxGenUtil::utf8toUTF16(line)); })
                        == outputLO.end()
                    && filesystem::exists(m_gameDataPath / line)) {

                    outputLO.push_back(ParallaxGenUtil::utf8toUTF16(line));
                }
            }
        }
    }

    // Get the plugins file
    const filesystem::path pluginsFile = getPluginsFile();
    ifstream pluginsFileHandle(pluginsFile, 1);
    if (!pluginsFileHandle.is_open()) {
        if (m_logging) {
            spdlog::critical("Unable to open plugins.txt");
            exit(1);
        } else {
            throw runtime_error("Unable to open plugins.txt");
        }
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

            if (std::ranges::find_if(outputLO,
                    [&](const std::wstring& s) { return boost::iequals(s, ParallaxGenUtil::utf8toUTF16(line)); })
                    == outputLO.end()
                && filesystem::exists(m_gameDataPath / ParallaxGenUtil::utf8toUTF16(line))) {

                outputLO.push_back(ParallaxGenUtil::utf8toUTF16(line));
            }
        }
    }

    // close file handle
    pluginsFileHandle.close();

    if (trimExtension) {
        // Trim the extension from the plugin name
        for (wstring& plugin : outputLO) {
            plugin = plugin.substr(0, plugin.find_last_of(L'.'));
        }
    }

    if (lowercase) {
        // Convert the plugin name to lowercase
        for (wstring& plugin : outputLO) {
            boost::to_lower(plugin);
        }
    }

    if (m_logging) {
        wstring loadOrderStr = boost::algorithm::join(outputLO, L",");
        spdlog::debug(L"Active Plugin Load Order: {}", loadOrderStr);
    }

    return outputLO;
}

auto BethesdaGame::getGameDocumentSystemPath() const -> filesystem::path
{
    filesystem::path docPath = getSystemPath(FOLDERID_Documents);
    if (docPath.empty()) {
        return {};
    }

    docPath /= getDocumentLocation();
    return docPath;
}

auto BethesdaGame::getGameAppdataSystemPath() const -> filesystem::path
{
    filesystem::path appDataPath = getSystemPath(FOLDERID_LocalAppData);
    if (appDataPath.empty()) {
        return {};
    }

    appDataPath /= getAppDataLocation();
    return appDataPath;
}

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

auto BethesdaGame::getGameTypes() -> vector<GameType>
{
    const static auto gameTypes = vector<GameType> { GameType::SKYRIM_SE, GameType::SKYRIM_GOG, GameType::SKYRIM,
        GameType::SKYRIM_VR, GameType::ENDERAL, GameType::ENDERAL_SE };
    return gameTypes;
}

auto BethesdaGame::getStrFromGameType(const GameType& type) -> string
{
    const static auto gameTypeToStrMap = unordered_map<GameType, string> { { GameType::SKYRIM_SE, "Skyrim SE" },
        { GameType::SKYRIM_GOG, "Skyrim GOG" }, { GameType::SKYRIM, "Skyrim" }, { GameType::SKYRIM_VR, "Skyrim VR" },
        { GameType::ENDERAL, "Enderal" }, { GameType::ENDERAL_SE, "Enderal SE" } };

    if (gameTypeToStrMap.contains(type)) {
        return gameTypeToStrMap.at(type);
    }

    return gameTypeToStrMap.at(GameType::SKYRIM_SE);
}

auto BethesdaGame::getGameTypeFromStr(const string& type) -> GameType
{
    const static auto strToGameTypeMap = unordered_map<string, GameType> { { "Skyrim SE", GameType::SKYRIM_SE },
        { "Skyrim GOG", GameType::SKYRIM_GOG }, { "Skyrim", GameType::SKYRIM }, { "Skyrim VR", GameType::SKYRIM_VR },
        { "Enderal", GameType::ENDERAL }, { "Enderal SE", GameType::ENDERAL_SE } };

    if (strToGameTypeMap.contains(type)) {
        return strToGameTypeMap.at(type);
    }

    return strToGameTypeMap.at("Skyrim SE");
}
