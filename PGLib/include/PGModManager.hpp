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
private:
    constexpr static uint8_t hexAlphaBase = 10U;

    /// @brief CreateToolhelp32Snapshot() fails with ERROR_BAD_LENGTH when the module list changes while the snapshot
    /// is taken; the documented handling is to retry, this bounds the retries.
    constexpr static unsigned moduleSnapshotMaxAttempts = 16U;

    /**
     * @brief Converts a single hexadecimal character to its integer value.
     *
     * @param c The hexadecimal character (0-9, a-f, A-F).
     * @return uint8_t value of the hex digit, or 0 for invalid input.
     */
    static uint8_t fromHexDigit(char c);

    /**
     * @brief Decodes a MO2 INI field value that is encoded as a Qt byte array (e.g., "@ByteArray(68656C6C6F)").
     *
     * @param byteArrayVal The string value extracted from the MO2 INI field, without the "@ByteArray(" prefix and ")"
     * suffix.
     * @return std::wstring containing the decoded value.
     */
    static std::wstring decodeQtByteArrayValue(const std::string& byteArrayVal);

public:
    /// @brief Identifies which mod manager type is in use.
    enum class ModManagerType : uint8_t { None, Vortex, ModOrganizer2 };

    /**
     * @brief Represents a single mod entry with its metadata and conflict information.
     */
    struct Mod {
        // Hash function for Mod struct shared pointer that hashes only name.
        struct ModHash {
            std::size_t operator()(const std::shared_ptr<Mod>& mod) const
            {
                return std::hash<std::wstring>()(mod->name);
            }
        };

        /// @brief Mutex protecting concurrent modifications to this mod's fields.
        std::shared_mutex mutex;
        /// @brief The mod's display name as used by the mod manager.
        std::wstring name;
        /// @brief The absolute path to the mod's folder on disk.
        std::filesystem::path folder;
        /// @brief True if this mod was added since the last session and has no stored priority.
        bool isNew = false;
        /// @brief True if the mod is currently enabled in the mod manager.
        bool isEnabled = false;
        /// @brief True if mesh files from this mod are excluded from patching.
        bool areMeshesIgnored = false;
        /// @brief True if the mod contains at least one NIF mesh file.
        bool hasMeshes = false;
        /// @brief Index in the mod manager's native ordering (used as a secondary sort key).
        int modManagerOrder = 0;
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
    std::filesystem::path m_stagingLocation;
    bool m_didLoadModRules = false;

    static constexpr const char* mo2IniProfilesDirKey = "profiles_directory=";
    static constexpr const char* mo2IniModDirKey = "mod_directory=";
    static constexpr const char* mo2IniBaseDirKey = "base_directory=";
    static constexpr const char* mo2IniGameDirKey = "gamePath=";
    static constexpr const char* mo2IniProfileKey = "selected_profile=";
    static constexpr const char* mo2IniGameNameKey = "gameName=";
    static constexpr const char* mo2IniGameEditionKey = "game_edition=";
    static constexpr const char* mo2IniBaseDirWildcard = "%BASE_DIR%";

    static constexpr const char* mo2IniByteArrayPrefix = "@ByteArray(";
    static constexpr const char* mo2IniByteArraySuffix = ")";

    /// @brief MO2's virtual filesystem DLL, injected from the MO2 install folder into every process MO2 launches.
    static constexpr const wchar_t* mo2UsvfsDLLName = L"usvfs_x64.dll";
    static constexpr const wchar_t* mo2ExeFilename = L"ModOrganizer.exe";

public:
    /**
     * @brief Constructs a PGModManager for the given mod manager type. No mods are known until one of the populate
     * functions ran.
     *
     * @param mmType The mod manager type (None, Vortex, or ModOrganizer2).
     */
    explicit PGModManager(const ModManagerType& mmType);

    /**
     * @brief Returns the map of relative file paths to the mods that own them.
     *
     * @return Const reference to the file-to-mod map.
     */
    [[nodiscard]] const std::unordered_map<std::filesystem::path,
                                           std::shared_ptr<Mod>>&
    modFileMap() const;

    /**
     * @brief Finds the mod that owns the given relative file path.
     *
     * @param relPath Relative path of the file (as used in the data directory).
     * @return Shared pointer to the owning Mod, or nullptr if not found.
     */
    [[nodiscard]] std::shared_ptr<Mod> modByFile(const std::filesystem::path& relPath) const;

    /// @brief The mod manager type this manager was created for.
    [[nodiscard]] ModManagerType type() const { return m_mmType; }

    /**
     * @brief Finds the mod for a file, accounting for BSA-packed files by resolving the mod-lookup path via PGD. The
     * lookup is recorded for the update cache.
     *
     * @param relPath Lower case relative path of the file.
     * @return Shared pointer to the owning Mod, or nullptr if not found.
     * @throws std::runtime_error if PGGlobals::pgd() is not set.
     */
    [[nodiscard]] std::shared_ptr<Mod> modByFileSmart(const std::filesystem::path& relPath) const;

    /**
     * @brief Returns all known mods (excluding the empty/virtual mod entry).
     *
     * @return Vector of shared pointers to all Mod objects.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Mod>> mods() const;

    /**
     * @brief Returns all mods sorted by descending priority (highest priority first), then by mod manager order.
     *
     * @return Priority-sorted vector of mods.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Mod>> modsByPriority() const;

    /**
     * @brief Returns all mods sorted by their native mod manager order (ascending).
     *
     * @return Default-order-sorted vector of mods.
     */
    [[nodiscard]] std::vector<std::shared_ptr<Mod>> modsByDefaultOrder() const;

    /**
     * @brief Finds a mod by its display name.
     *
     * @param modName Wide-string mod name.
     * @return Shared pointer to the Mod, or nullptr if not found.
     */
    [[nodiscard]] std::shared_ptr<Mod> mod(const std::wstring& modName) const;

    /**
     * @brief Deserializes mod priority/enabled/meshesignored state from a JSON object into the mod map, creating the
     * mods that are not known yet. Loaded mods are not new.
     *
     * @param json JSON object mapping mod names to their stored properties.
     * @throws std::runtime_error if the JSON structure is invalid.
     */
    void loadJSON(const nlohmann::json& json);

    /**
     * @brief Returns whether mod rules were loaded from an existing modrules.json via loadJSON().
     *
     * @return true if loadJSON() has been called (modrules.json existed at startup); false otherwise.
     */
    [[nodiscard]] bool hasLoadedModRules() const;

    /**
     * @brief Serializes all mods' priority and enabled state to a JSON object.
     *
     * @return JSON object mapping mod names to their properties.
     */
    nlohmann::json json();

    /**
     * @brief Checks whether the given directory is a valid MO2 instance (contains modorganizer.ini).
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return true if the instance directory is valid; false otherwise.
     */
    static bool isValidMO2InstanceDir(const std::filesystem::path& instanceDir);

    /**
     * @brief Reads the game installation path from the MO2 instance's modorganizer.ini.
     *
     * A relative gamePath value is resolved with resolveMO2GamePath(). If the MO2 folder cannot be found, the relative
     * value is returned as-is so that PGConfig::validateParams() can report it.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return Game directory (absolute unless a relative value could not be resolved), or empty if modorganizer.ini
     * has no gamePath.
     */
    static std::filesystem::path gamePathFromInstanceDir(const std::filesystem::path& instanceDir);

    /**
     * @brief Finds the folder containing ModOrganizer.exe of the MO2 that launched this process.
     *
     * MO2 injects usvfs_x64.dll from its own install folder into every process it launches, so the folder of that
     * loaded module is the MO2 install folder. A non-empty result also means the process runs under MO2's virtual
     * filesystem.
     *
     * @return Folder containing ModOrganizer.exe, or empty if usvfs_x64.dll is not loaded (PGPatcher was not launched
     * from MO2).
     */
    [[nodiscard]] static std::filesystem::path mo2DirFromUSVFS();

    /**
     * @brief Finds the folder containing ModOrganizer.exe for the given instance.
     *
     * The primary source is the usvfs DLL MO2 injected into this process (mo2DirFromUSVFS()), which is exact for
     * portable and global instances alike. If PGPatcher was not launched from MO2, the instance folder is used when it
     * contains ModOrganizer.exe (portable instance). Global (%LOCALAPPDATA%) instances record nothing about the MO2
     * folder, so for those it is unknown unless PGPatcher was launched from MO2.
     *
     * @param instanceDir Path to the MO2 instance directory (folder containing modorganizer.ini).
     * @return Absolute path to the folder containing ModOrganizer.exe, or empty if it cannot be determined.
     */
    [[nodiscard]] static std::filesystem::path findMO2Dir(const std::filesystem::path& instanceDir);

    /**
     * @brief Resolves a gamePath value from modorganizer.ini to an absolute path the way MO2 does.
     *
     * MO2 sets its working directory to the folder containing ModOrganizer.exe (MOApplication constructor,
     * QDir::setCurrent(applicationDirPath())) and hands gamePath to the game plugin unchanged (Instance::setup ->
     * IPluginGame::setGamePath), so a relative gamePath is relative to the folder containing ModOrganizer.exe, which
     * is taken from findMO2Dir().
     *
     * @param gamePath gamePath value as stored in modorganizer.ini (absolute or relative).
     * @param instanceDir Path to the MO2 instance directory (folder containing modorganizer.ini).
     * @return Absolute, lexically normalized game path. Empty and absolute inputs are returned unchanged, and so is a
     * relative input when findMO2Dir() cannot find the MO2 folder.
     */
    [[nodiscard]] static std::filesystem::path resolveMO2GamePath(const std::filesystem::path& gamePath,
                                                                  const std::filesystem::path& instanceDir);

    /**
     * @brief Determines the BethesdaGame::GameType from the MO2 instance's modorganizer.ini.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return The detected GameType, or GameType::Unknown if unrecognized.
     */
    static BethesdaGame::GameType gameTypeFromInstanceDir(const std::filesystem::path& instanceDir);

    /**
     * @brief Reads the selected MO2 profile name from the instance's modorganizer.ini.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @return Wide-string profile name, or empty if not found.
     */
    static std::wstring selectedProfileFromInstanceDir(const std::filesystem::path& instanceDir);

    /**
     * @brief Populates the mod file map from the active MO2 profile's modlist.txt: every enabled mod's files under the
     * folders to map and its archives, the first mod in the list winning. Mods that are not in the list anymore are
     * forgotten.
     *
     * A mod folder that is missing or that is the output directory stops the population with a critical error.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @param outputDir Path to the PGPatcher output directory, which must not be an enabled mod.
     * @throws std::runtime_error if modorganizer.ini or modlist.txt does not exist.
     */
    void populateModFileMapMO2(const std::filesystem::path& instanceDir,
                               const std::filesystem::path& outputDir);

    /**
     * @brief Populates the mod file map from vortex.deployment.json in the given directory. Only files under the
     * folders to map are mapped; mods that are not deployed anymore are forgotten.
     *
     * @param deploymentDir Directory holding vortex.deployment.json (the game's data folder).
     * @throws std::runtime_error if the manifest is missing or lacks the "files" or "stagingPath" field.
     */
    void populateModFileMapVortex(const std::filesystem::path& deploymentDir);

    // Helpers.
    /**
     * @brief Returns a list of all supported ModManagerType values.
     *
     * @return Vector containing None, Vortex, and ModOrganizer2.
     */
    [[nodiscard]] static std::vector<ModManagerType> modManagerTypes();

    /**
     * @brief Converts a ModManagerType enum value to its display string.
     *
     * @param type The mod manager type.
     * @return String name (e.g., "None", "Vortex", "Mod Organizer 2").
     */
    [[nodiscard]] static std::string strFromModManagerType(const ModManagerType& type);

    /**
     * @brief Converts a display string to the corresponding ModManagerType enum value.
     *
     * @param type String name of the mod manager type.
     * @return Corresponding ModManagerType, or None if unrecognized.
     */
    [[nodiscard]] static ModManagerType modManagerTypeFromStr(const std::string& type);

    /**
     * @brief Returns the root staging location used by the active mod manager.
     */
    [[nodiscard]] const std::filesystem::path& stagingLocation() const;

    /**
     * @brief Enables new mods and renumbers priorities the way the mod sort dialog saves them.
     *
     * New mods (not in modrules.json) that provide a shader better than the default one are enabled and given
     * priorities above the current maximum. Then the mods the dialog displays (mods with shaders or meshes) are
     * ordered enabled first, disabled second, keeping the relative order of the base ordering, and the enabled ones get
     * contiguous descending priorities. Hidden mods keep their stored priority. Call after mapFiles() completed, so
     * every mod's shaders are known.
     *
     * @param useDefaultOrder If true (and the mod manager is MO2), the base ordering is modsByDefaultOrder();
     * otherwise modsByPriority().
     */
    void updateStateFromModlist(bool useDefaultOrder) const;

    /**
     * @brief Records that the mod owning a file provides a shader type. Nothing happens for a file without a mod.
     */
    void addShaderToModByFile(const std::filesystem::path& relPath,
                              const PGEnums::ShapeShader& shader) const;

private:
    /**
     * @brief Orders two mods: by priority (higher first) when checkPriority is set, then by mod manager order (lower
     * first), then by best shader (better first, mods with shaders before mods without), then by name.
     */
    [[nodiscard]] static bool compareMods(const std::shared_ptr<Mod>& a,
                                          const std::shared_ptr<Mod>& b,
                                          bool checkPriority = true);

    /**
     * @brief Reads the value of a "key=" line of modorganizer.ini, without surrounding quotes and with doubled
     * backslashes collapsed. @ByteArray(...) values are decoded when isByteArray is set.
     *
     * @return The value, or an empty string if the file or the key does not exist.
     */
    static std::wstring mo2INIField(const std::filesystem::path& instanceDir,
                                    const std::string& fieldName,
                                    const bool& isByteArray = false);

    /**
     * @brief Returns the profiles and mods folders of an MO2 instance from modorganizer.ini, with %BASE_DIR% expanded
     * and the instance folder's "profiles" and "mods" as defaults.
     */
    static std::pair<std::filesystem::path,
                     std::filesystem::path>
    mo2FilePaths(const std::filesystem::path& instanceDir);
};
