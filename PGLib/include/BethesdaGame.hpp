#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <windows.h>

// Steam game ID definitions
enum class SteamGameID : int {
    STEAMGAMEID_SKYRIM_SE = 489830,
    STEAMGAMEID_SKYRIM_VR = 611670,
    STEAMGAMEID_SKYRIM = 72850,
    STEAMGAMEID_ENDERAL = 933480,
    STEAMGAMEID_ENDERAL_SE = 976620
};

constexpr unsigned REG_BUFFER_SIZE = 1024;

class BethesdaGame {
public:
    // GameType enum
    enum class GameType : uint8_t { SKYRIM_SE, SKYRIM_GOG, SKYRIM_VR, SKYRIM, ENDERAL, ENDERAL_SE, UNKNOWN };

    // StoreType enum (for now only Steam is used)
    enum class StoreType : uint8_t { STEAM, WINDOWS_STORE, EPIC_GAMES_STORE, GOG };

    // struct that stores location of ini and custom ini file for a game
    struct ININame {
        std::filesystem::path ini;
        std::filesystem::path iniPrefs;
        std::filesystem::path iniCustom;
    };

private:
    [[nodiscard]] auto getINILocations() const -> ININame;
    [[nodiscard]] auto getDocumentLocation() const -> std::filesystem::path;
    [[nodiscard]] static auto getAppDataLocation(const GameType& type) -> std::filesystem::path;
    [[nodiscard]] auto getSteamGameID() const -> int;
    [[nodiscard]] static auto getDataCheckFile(const GameType& type) -> std::filesystem::path;

    // stores the game type
    GameType m_objGameType;

    // stores game path and game data path (game path / data)
    std::filesystem::path m_gamePath;
    std::filesystem::path m_gameDataPath;
    std::filesystem::path m_gameAppDataPath;
    std::filesystem::path m_gameDocumentPath;

public:
    // constructor
    BethesdaGame(GameType gameType, const std::filesystem::path& gamePath = "",
        const std::filesystem::path& appDataPath = "", const std::filesystem::path& documentPath = "");

    // get functions
    [[nodiscard]] auto getGameType() const -> GameType;
    [[nodiscard]] auto getGamePath() const -> std::filesystem::path;
    [[nodiscard]] auto getGameDataPath() const -> std::filesystem::path;

    [[nodiscard]] auto getINIPaths() const -> ININame;
    [[nodiscard]] auto getPluginsFile() const -> std::filesystem::path;

    // Get number of active plugins including Bethesda master files
    [[nodiscard]] auto getActivePlugins(const bool& trimExtension = false, const bool& lowercase = false) const
        -> std::vector<std::wstring>;

    // Helpers
    [[nodiscard]] static auto getGameTypes() -> std::vector<GameType>;
    [[nodiscard]] static auto getStrFromGameType(const GameType& type) -> std::string;
    [[nodiscard]] static auto getGameTypeFromStr(const std::string& type) -> GameType;
    [[nodiscard]] static auto isGamePathValid(const std::filesystem::path& gamePath, const GameType& type) -> bool;

    // locates the steam install locatino of steam
    [[nodiscard]] static auto findGamePathFromSteam(const GameType& type) -> std::filesystem::path;

private:
    [[nodiscard]] auto getGameDocumentSystemPath() const -> std::filesystem::path;
    [[nodiscard]] static auto getGameAppdataSystemPath(const GameType& type) -> std::filesystem::path;

    // gets the system path for a folder (from windows.h)
    static auto getSystemPath(const GUID& folderID) -> std::filesystem::path;

    [[nodiscard]] static auto getGameRegistryPath(const GameType& type) -> std::string;
};
