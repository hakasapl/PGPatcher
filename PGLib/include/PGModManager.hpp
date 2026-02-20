#pragma once

#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"


#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/**
 * @brief Manages the mapping between mod files and their owning mods, supporting Mod Organizer 2 and Vortex.
 *
 * Provides lookup of mods by file path, priority ordering, JSON serialization, and utility helpers
 * for reading MO2 instance configuration files.
 */
class PGModManager {

public:
    /// @brief Identifies which mod manager type is in use.
    enum class ModManagerType : uint8_t { NONE, VORTEX, MODORGANIZER2 };

    /**
     * @brief Represents a single mod entry with its metadata and conflict information.
     */
    struct Mod {
        // Hash function for Mod struct shared pointer that hashes only name
        struct ModHash {
            auto operator()(const std::shared_ptr<Mod>& mod) const -> std::size_t
            {
                return std::hash<std::wstring>()(mod->name);
            }
        };

        /// @brief Mutex protecting concurrent modifications to this mod's fields.
        std::shared_mutex mutex;
        /// @brief The mod's display name as used by the mod manager.
        std::wstring name;
        /// @brief True if this mod was added since the last session and has no stored priority.
        bool isNew = false;
        /// @brief True if the mod is currently enabled in the mod manager.
        bool isEnabled = false;
        /// @brief True if mesh files from this mod are excluded from patching.
        bool areMeshesIgnored = false;
        /// @brief True if the mod contains at least one NIF mesh file.
        bool hasMeshes = false;
        /// @brief Index in the mod manager's native ordering (used as a secondary sort key).
        int modManagerOrder;
        /// @brief User-assigned patch priority; higher values are applied later (win over lower).
        int priority = -1;
        /// @brief Set of shader types used by shapes within this mod's meshes.
        std::set<PGEnums::ShapeShader> shaders;
        /// @brief Other mods whose files overlap with this mod's files.
        std::unordered_set<std::shared_ptr<Mod>, ModHash> conflicts;
    };

private:
    std::unordered_map<std::wstring, std::shared_ptr<Mod>> m_modMap;
    std::unordered_map<std::filesystem::path, std::shared_ptr<Mod>> m_modFileMap;

    ModManagerType m_mmType;

    static constexpr const char* MO2INI_PROFILESDIR_KEY = "profiles_directory=";
    static constexpr const char* MO2INI_MODDIR_KEY = "mod_directory=";
    static constexpr const char* MO2INI_BASEDIR_KEY = "base_directory=";
    static constexpr const char* MO2INI_GAMEDIR_KEY = "gamePath=";
    static constexpr const char* MO2INI_PROFILE_KEY = "selected_profile=";
    static constexpr const char* MO2INI_GAMENAME_KEY = "gameName=";
    static constexpr const char* MO2INI_GAMEEDITION_KEY = "game_edition=";
    static constexpr const char* MO2INI_BASEDIR_WILDCARD = "%BASE_DIR%";

    static constexpr const char* MO2INI_BYTEARRAYPREFIX = "@ByteArray(";
    static constexpr const char* MO2INI_BYTEARRAYSUFFIX = ")";

public:
    /**
     * @brief Constructs a PGModManager for the given mod manager type.
     *
     * @param mmType The mod manager type (NONE, VORTEX, or MODORGANIZER2).
     */
    PGModManager(const ModManagerType& mmType);

    /**
     * @brief Returns the map of relative file paths to the mods that own them.
     *
     * @return Const reference to the file-to-mod map.
     */
    [[nodiscard]] auto getModFileMap() const -> const std::unordered_map<std::filesystem::path,
                                                                         std::shared_ptr<Mod>>&;

    /**
     * @brief Finds the mod that owns the given relative file path.
     *
     * @param relPath Relative path of the file (as used in the data directory).
     * @return Shared pointer to the owning Mod, or nullptr if not found.
     */
    [[nodiscard]] auto getModByFile(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>;

    /**
     * @brief Finds the mod for a file, accounting for BSA-packed files by resolving the mod-lookup path via PGD.
     *
     * @param relPath Relative path of the file.
     * @return Shared pointer to the owning Mod, or nullptr if not found.
     */
    [[nodiscard]] auto getModByFileSmart(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>;

    /**
     * @brief Returns all known mods (excluding the empty/virtual mod entry).
     *
     * @return Vector of shared pointers to all Mod objects.
     */
    [[nodiscard]] auto getMods() const -> std::vector<std::shared_ptr<Mod>>;

    /**
     * @brief Returns all mods sorted by descending priority (highest priority first), then by mod manager order.
     *
     * @return Priority-sorted vector of mods.
     */
    [[nodiscard]] auto getModsByPriority() const -> std::vector<std::shared_ptr<Mod>>;

    /**
     * @brief Returns all mods sorted by their native mod manager order (ascending).
     *
     * @return Default-order-sorted vector of mods.
     */
    [[nodiscard]] auto getModsByDefaultOrder() const -> std::vector<std::shared_ptr<Mod>>;

    /**
     * @brief Finds a mod by its display name.
     *
     * @param modName Wide-string mod name.
     * @return Shared pointer to the Mod, or nullptr if not found.
     */
    [[nodiscard]] auto getMod(const std::wstring& modName) const -> std::shared_ptr<Mod>;

    /**
     * @brief Deserializes mod priority/enabled state from a JSON object into the mod map.
     *
     * @param json JSON object mapping mod names to their stored properties.
     * @throws std::runtime_error if the JSON structure is invalid.
     */
    void loadJSON(const nlohmann::json& json);

    /**
     * @brief Serializes all mods' priority and enabled state to a JSON object.
     *
     * @return JSON object mapping mod names to their properties.
     */
    auto getJSON() -> nlohmann::json;

    /**
     * @brief Checks whether the given directory is a valid MO2 instance (contains modorganizer.ini).
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return true if the instance directory is valid; false otherwise.
     */
    static auto isValidMO2InstanceDir(const std::filesystem::path& instanceDir) -> bool;

    /**
     * @brief Reads the game installation path from the MO2 instance's modorganizer.ini.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return Absolute path to the game directory.
     */
    static auto getGamePathFromInstanceDir(const std::filesystem::path& instanceDir) -> std::filesystem::path;

    /**
     * @brief Determines the BethesdaGame::GameType from the MO2 instance's modorganizer.ini.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return The detected GameType, or GameType::UNKNOWN if unrecognized.
     */
    static auto getGameTypeFromInstanceDir(const std::filesystem::path& instanceDir) -> BethesdaGame::GameType;

    /**
     * @brief Reads the selected MO2 profile name from the instance's modorganizer.ini.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return Wide-string profile name, or empty if not found.
     */
    static auto getSelectedProfileFromInstanceDir(const std::filesystem::path& instanceDir) -> std::wstring;

    /**
     * @brief Populates the mod file map by reading the active MO2 profile's modlist.txt and mod directories.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @param outputDir Path to the PGPatcher output directory (treated as a special mod entry).
     */
    void populateModFileMapMO2(const std::filesystem::path& instanceDir,
                               const std::filesystem::path& outputDir);

    /**
     * @brief Populates the mod file map by reading Vortex's deployment manifest from the given directory.
     *
     * @param deploymentDir Path to the Vortex deployment directory.
     */
    void populateModFileMapVortex(const std::filesystem::path& deploymentDir);

    // Helpers
    /**
     * @brief Returns a list of all supported ModManagerType values.
     *
     * @return Vector containing NONE, VORTEX, and MODORGANIZER2.
     */
    [[nodiscard]] static auto getModManagerTypes() -> std::vector<ModManagerType>;

    /**
     * @brief Converts a ModManagerType enum value to its display string.
     *
     * @param type The mod manager type.
     * @return String name (e.g., "None", "Vortex", "Mod Organizer 2").
     */
    [[nodiscard]] static auto getStrFromModManagerType(const ModManagerType& type) -> std::string;

    /**
     * @brief Converts a display string to the corresponding ModManagerType enum value.
     *
     * @param type String name of the mod manager type.
     * @return Corresponding ModManagerType, or NONE if unrecognized.
     */
    [[nodiscard]] static auto getModManagerTypeFromStr(const std::string& type) -> ModManagerType;

    /**
     * @brief Assigns ascending priority values to all newly-added enabled mods that have no stored priority.
     *
     * New mods are sorted by shader quality and name, then assigned priorities above the current maximum.
     */
    void assignNewModPriorities() const;

private:
    [[nodiscard]] static auto compareMods(const std::shared_ptr<Mod>& a,
                                          const std::shared_ptr<Mod>& b,
                                          bool checkPriority = true) -> bool;

    static auto getMO2INIField(const std::filesystem::path& instanceDir,
                               const std::string& fieldName,
                               const bool& isByteArray = false) -> std::wstring;

    static auto getMO2FilePaths(const std::filesystem::path& instanceDir) -> std::pair<std::filesystem::path,
                                                                                       std::filesystem::path>;
};
