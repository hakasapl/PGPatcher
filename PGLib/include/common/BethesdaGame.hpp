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
    STEAMGAMEID_ENDERAL_SE = 976620
};

constexpr unsigned REG_BUFFER_SIZE = 1024;

/**
 * @brief Represents a Bethesda RPG game installation, tracking the game type and paths,
 *        and providing utilities for managing plugins and INI files.
 */
class BethesdaGame {
public:
    /**
     * @brief Identifies the specific Bethesda game variant.
     */
    enum class GameType : uint8_t {
        SKYRIM_SE,    ///< Skyrim Special Edition (Steam)
        SKYRIM_GOG,   ///< Skyrim Special Edition (GOG)
        SKYRIM_VR,    ///< Skyrim VR (Steam)
        ENDERAL_SE,   ///< Enderal Special Edition (Steam)
        UNKNOWN       ///< Unknown or unsupported game type
    };

    /**
     * @brief Identifies the store/platform through which the game was purchased.
     */
    enum class StoreType : uint8_t {
        STEAM,              ///< Steam store
        WINDOWS_STORE,      ///< Microsoft / Windows Store
        EPIC_GAMES_STORE,   ///< Epic Games Store
        GOG                 ///< GOG (Good Old Games)
    };

    /**
     * @brief Holds the paths to a game's INI configuration files.
     */
    struct ININame {
        std::filesystem::path ini;        ///< Path to the primary INI file (e.g. skyrim.ini)
        std::filesystem::path iniPrefs;   ///< Path to the preferences INI file (e.g. skyrimprefs.ini)
        std::filesystem::path iniCustom;  ///< Path to the custom INI file (e.g. skyrimcustom.ini)
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
    /**
     * @brief Constructs a BethesdaGame instance for the specified game type and paths.
     *
     * @param gameType    The type of game (e.g. SKYRIM_SE, SKYRIM_VR).
     * @param gamePath    Path to the game's root installation directory. If empty, Steam registry is queried.
     * @param appDataPath Path to the game's AppData directory. If empty, the system default is used.
     * @param documentPath Path to the game's Documents directory. If empty, the system default is used.
     */
    BethesdaGame(GameType gameType,
                 const std::filesystem::path& gamePath = "",
                 const std::filesystem::path& appDataPath = "",
                 const std::filesystem::path& documentPath = "");

    /**
     * @brief Returns the game type of this instance.
     *
     * @return The GameType enum value representing this game.
     */
    [[nodiscard]] auto getGameType() const -> GameType;

    /**
     * @brief Returns the root installation path of the game.
     *
     * @return Filesystem path to the game's installation directory.
     */
    [[nodiscard]] auto getGamePath() const -> std::filesystem::path;

    /**
     * @brief Returns the path to the game's Data directory.
     *
     * @return Filesystem path to the game's Data subdirectory.
     */
    [[nodiscard]] auto getGameDataPath() const -> std::filesystem::path;

    /**
     * @brief Returns the fully resolved paths to the game's INI configuration files.
     *
     * @return ININame struct containing absolute paths to the primary, preferences, and custom INI files.
     */
    [[nodiscard]] auto getINIPaths() const -> ININame;

    /**
     * @brief Returns the path to the game's plugins.txt file.
     *
     * @return Filesystem path to plugins.txt in the game's AppData directory.
     */
    [[nodiscard]] auto getPluginsFile() const -> std::filesystem::path;

    /**
     * @brief Returns the list of active plugins including Bethesda master files.
     *
     * @param trimExtension If true, file extensions are stripped from plugin names.
     * @param lowercase     If true, plugin names are converted to lowercase.
     * @return Vector of wide strings containing the active plugin names in load order.
     */
    [[nodiscard]] auto getActivePlugins(const bool& trimExtension = false,
                                        const bool& lowercase = false) const -> std::vector<std::wstring>;

    /**
     * @brief Returns all supported game types.
     *
     * @return Vector of all known GameType enum values (excluding UNKNOWN).
     */
    [[nodiscard]] static auto getGameTypes() -> std::vector<GameType>;

    /**
     * @brief Converts a GameType enum value to its human-readable string representation.
     *
     * @param type The GameType to convert.
     * @return String such as "Skyrim SE" or "Enderal SE".
     */
    [[nodiscard]] static auto getStrFromGameType(const GameType& type) -> std::string;

    /**
     * @brief Converts a human-readable game type string to the corresponding GameType enum value.
     *
     * @param type String such as "Skyrim SE" or "Enderal SE".
     * @return The matching GameType enum value, defaulting to SKYRIM_SE if not recognized.
     */
    [[nodiscard]] static auto getGameTypeFromStr(const std::string& type) -> GameType;

    /**
     * @brief Checks whether the given directory is a valid installation path for the specified game type.
     *
     * @param gamePath Path to the candidate game installation directory.
     * @param type     The game type to validate against.
     * @return true if the path contains the expected game data files; false otherwise.
     */
    [[nodiscard]] static auto isGamePathValid(const std::filesystem::path& gamePath,
                                              const GameType& type) -> bool;

    /**
     * @brief Attempts to locate the game's installation path via the Steam registry.
     *
     * @param type The game type to search for.
     * @return Filesystem path to the game's installation directory, or an empty path if not found.
     */
    [[nodiscard]] static auto findGamePathFromSteam(const GameType& type) -> std::filesystem::path;

private:
    [[nodiscard]] auto getGameDocumentSystemPath() const -> std::filesystem::path;
    [[nodiscard]] static auto getGameAppdataSystemPath(const GameType& type) -> std::filesystem::path;

    // gets the system path for a folder (from windows.h)
    static auto getSystemPath(const GUID& folderID) -> std::filesystem::path;

    [[nodiscard]] static auto getGameRegistryPath(const GameType& type) -> std::string;
};
