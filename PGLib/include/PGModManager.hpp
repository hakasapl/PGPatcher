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

class MO2DataFolder;

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
    static constexpr const char* mo2IniOverwriteDirKey = "overwrite_directory=";
    static constexpr const char* mo2IniSkipFileSuffixesKey = "skip_file_suffixes=";
    static constexpr const char* mo2IniSkipDirectoriesKey = "skip_directories=";
    static constexpr const char* mo2IniBaseDirWildcard = "%BASE_DIR%";
    /// @brief The value QSettings writes for an empty list.
    static constexpr const wchar_t* mo2IniInvalidValue = L"@Invalid()";
    /// @brief MO2's file name suffix of hidden files, the one file suffix it skips by default.
    static constexpr const wchar_t* mo2DefaultSkipFileSuffix = L".mohidden";
    /// @brief The one folder name MO2 skips by default.
    static constexpr const wchar_t* mo2DefaultSkipDirectory = L".git";

    static constexpr const char* mo2IniByteArrayPrefix = "@ByteArray(";
    static constexpr const char* mo2IniByteArraySuffix = ")";

    /// @brief MO2's virtual filesystem DLL, injected from the MO2 install folder into every process MO2 launches.
    static constexpr const wchar_t* mo2UsvfsDLLName = L"usvfs_x64.dll";
    static constexpr const wchar_t* mo2ExeFilename = L"ModOrganizer.exe";
    static constexpr const wchar_t* mo2IniFileName = L"ModOrganizer.ini";
    /// @brief The file in the MO2 folder that locks MO2 to its portable instance.
    static constexpr const wchar_t* mo2PortableLockFileName = L"portable.txt";
    /// @brief The registry key and value MO2 keeps its current global instance in.
    static constexpr const wchar_t* mo2RegistryKey = L"Software\\Mod Organizer Team\\Mod Organizer";
    static constexpr const wchar_t* mo2RegistryCurrentInstanceValue = L"CurrentInstance";
    /// @brief The folder of the global instances below the local application data.
    static constexpr const wchar_t* mo2GlobalInstancesFolder = L"ModOrganizer";

public:
    /// @brief Vortex's deployment manifest in the data folder, which tells a Vortex deployment apart.
    static constexpr const wchar_t* vortexDeploymentFileName = L"vortex.deployment.json";

    /**
     * @struct Detection
     * @brief The mod manager a process runs under, see detect().
     */
    struct Detection {
        ModManagerType type = ModManagerType::None;
        std::filesystem::path mo2InstanceDir; /**< The MO2 instance, empty for the other types */
    };

    /**
     * @brief The defaults of the mod manager settings, from what the process runs under: Mod Organizer 2 when its
     * virtual file system is loaded into the process, with the instance that launched it (runningMO2InstanceDir();
     * empty when it was not found, which is reported), Vortex when the data folder holds its deployment manifest,
     * none otherwise. The settings keep what the user makes of them from there.
     *
     * @param dataFolder The game's data folder, where Vortex deploys. Empty for none: no Vortex then.
     * @param shouldLog Whether the outcome and the problems are logged.
     */
    [[nodiscard]] static Detection detect(const std::filesystem::path& dataFolder,
                                          bool shouldLog = true);

    /**
     * @brief The detection with the process's surroundings given, what detect() reads from the process.
     *
     * @param dataFolder The game's data folder, where Vortex deploys.
     * @param mo2Dir The folder of the MO2 whose virtual file system is loaded, empty when none is.
     * @param runningInstanceDir The instance that launched the process, empty when none was found.
     * @param shouldLog Whether the outcome and the problems are logged.
     */
    [[nodiscard]] static Detection detect(const std::filesystem::path& dataFolder,
                                          const std::filesystem::path& mo2Dir,
                                          const std::filesystem::path& runningInstanceDir,
                                          bool shouldLog);

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
     * @brief Populates the mod file map from a view of the instance's data folder (MO2DataFolder) instead of walking
     * the mod folders: the same mods in the same order, and every file owned by a mod (the first mod of the list with
     * a copy the walk would map, whichever file the game sees), with the rules of the walk (the folders to map and
     * the archives at the top level, no meta.ini). An output folder mod stops the population as the walk does; a mod
     * without a folder is not in the view (MO2 cannot serve it), where the walk stops.
     *
     * @param view The data folder view of the instance.
     * @param outputDir Path to the PGPatcher output directory, which must not be an enabled mod.
     */
    void populateModFileMapMO2(const MO2DataFolder& view,
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
     * @brief Returns the profiles and mods folders of an MO2 instance from modorganizer.ini, with %BASE_DIR% expanded
     * and the instance folder's "profiles" and "mods" as defaults.
     */
    [[nodiscard]] static std::pair<std::filesystem::path,
                                   std::filesystem::path>
    mo2FilePaths(const std::filesystem::path& instanceDir);

    /**
     * @brief Returns the overwrite folder of an MO2 instance from modorganizer.ini, with %BASE_DIR% expanded and the
     * instance folder's "overwrite" as the default.
     */
    [[nodiscard]] static std::filesystem::path mo2OverwriteDir(const std::filesystem::path& instanceDir);

    /**
     * @brief Returns the file name suffixes MO2 leaves out of its virtual file system (the skip_file_suffixes
     * setting of modorganizer.ini), .mohidden when the setting is absent.
     */
    [[nodiscard]] static std::vector<std::wstring> mo2SkipFileSuffixes(const std::filesystem::path& instanceDir);

    /**
     * @brief Returns the folder names MO2 leaves out of its virtual file system (the skip_directories setting of
     * modorganizer.ini), .git when the setting is absent.
     */
    [[nodiscard]] static std::vector<std::wstring> mo2SkipDirectories(const std::filesystem::path& instanceDir);

    /**
     * @brief Returns the names of the enabled mods of an MO2 profile in the order of its modlist.txt, the first one
     * winning: the lines that start with '+', without the separators. Disabled ('-') and unmanaged ('*') mods,
     * comments and empty lines are skipped.
     *
     * @param modListFile Path of the profile's modlist.txt.
     * @throws std::runtime_error if the file does not exist.
     */
    [[nodiscard]] static std::vector<std::wstring> mo2EnabledMods(const std::filesystem::path& modListFile);

    /**
     * @brief Finds the instance of the MO2 that launched this process, the way MO2 picks it at startup: the portable
     * instance of the MO2 folder when its lock file exists, else the global instance MO2 keeps as current in the
     * registry, else the portable instance when the MO2 folder has one.
     *
     * The choice is MO2's default; a run started with another instance on its command line is not seen, which
     * matchesActivePlugins() tells afterwards.
     *
     * @return The instance folder, or empty if the process was not launched from MO2 or no instance is found.
     */
    [[nodiscard]] static std::filesystem::path runningMO2InstanceDir();

    /**
     * @brief The choice of runningMO2InstanceDir() with the surroundings given, what it reads from the process.
     *
     * @param mo2Dir The folder of the MO2 whose virtual file system is loaded.
     * @param currentInstanceName The global instance MO2 keeps as current, empty for none.
     * @param globalInstancesDir The folder of the global instances, the "ModOrganizer" folder of the local
     * application data.
     * @return The instance folder, or empty if no instance is found.
     */
    [[nodiscard]] static std::filesystem::path runningMO2InstanceDir(const std::filesystem::path& mo2Dir,
                                                                     const std::wstring& currentInstanceName,
                                                                     const std::filesystem::path& globalInstancesDir);

    /**
     * @brief Checks that the selected profile of an instance is the one a process runs under: the profile's
     * plugins.txt and the active plugins file the game reads (which MO2 links from the profile) have the same
     * content. An MO2 started with another instance or profile on its command line fails this.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @param pluginsFile The active plugins file as the process sees it (Plugins.txt of the local application data).
     * @return false when both files are readable and differ; true otherwise, since nothing says they differ.
     */
    [[nodiscard]] static bool matchesActivePlugins(const std::filesystem::path& instanceDir,
                                                   const std::filesystem::path& pluginsFile);

    /**
     * @brief Whether an instance can be the one a process runs under Mod Organizer 2's virtual file system with, so
     * that its view is what the file system serves: not a portable instance of another MO2 (a portable instance is
     * run by the MO2 in its own folder alone), and with the process's active plugins (matchesActivePlugins()). An
     * instance that cannot be read passes; reading it reports the problem.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @param pluginsFile The active plugins file as the process sees it (Plugins.txt of the local application data).
     * @param mo2Dir The folder of the MO2 whose virtual file system is loaded (mo2DirFromUSVFS()).
     */
    [[nodiscard]] static bool canBeRunningInstance(const std::filesystem::path& instanceDir,
                                                   const std::filesystem::path& pluginsFile,
                                                   const std::filesystem::path& mo2Dir);

    /**
     * @brief The MO2 instance whose view (MO2DataFolder) is what the virtual file system serves to this process, so
     * that the file map can be built from it instead of listing the data folder through usvfs: the configured
     * instance when it is an instance folder that can be the running one (canBeRunningInstance()), else the instance
     * that launched the process (runningMO2InstanceDir()) on the same terms, else empty, which means the listing.
     * Empty without the virtual file system.
     *
     * @param configuredInstanceDir The configured MO2 instance, absolute; empty for none.
     * @param pluginsFile The active plugins file as the process sees it.
     */
    [[nodiscard]] static std::filesystem::path fileMapInstanceDir(const std::filesystem::path& configuredInstanceDir,
                                                                  const std::filesystem::path& pluginsFile);

    /**
     * @brief fileMapInstanceDir() with the process's surroundings given.
     *
     * @param mo2Dir The folder of the MO2 whose virtual file system is loaded, empty when none is.
     * @param runningInstanceDir The instance that launched the process, empty when none was found.
     */
    [[nodiscard]] static std::filesystem::path fileMapInstanceDir(const std::filesystem::path& configuredInstanceDir,
                                                                  const std::filesystem::path& pluginsFile,
                                                                  const std::filesystem::path& mo2Dir,
                                                                  const std::filesystem::path& runningInstanceDir);

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
     * @brief Reads the value of a "key=" line of modorganizer.ini as the file has it.
     *
     * @return The value, or an empty string if the file or the key does not exist.
     */
    static std::string mo2INIRawField(const std::filesystem::path& instanceDir,
                                      const std::string& fieldName);

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
     * @brief Returns a folder setting of modorganizer.ini as a path, with %BASE_DIR% expanded, or the default folder
     * below the base directory when the setting is absent.
     *
     * @param instanceDir Path to the MO2 instance directory.
     * @param fieldName The "key=" of the setting.
     * @param defaultName Name of the default folder below the base directory.
     */
    static std::filesystem::path mo2FolderSetting(const std::filesystem::path& instanceDir,
                                                  const std::string& fieldName,
                                                  const wchar_t* defaultName);

    /**
     * @brief Returns a list setting of modorganizer.ini the way QSettings writes a string list: comma separated,
     * quoted when needed, @Invalid() for an empty list. The default is returned when the setting is absent.
     */
    static std::vector<std::wstring> mo2ListSetting(const std::filesystem::path& instanceDir,
                                                    const std::string& fieldName,
                                                    const std::vector<std::wstring>& defaultValue);

    /**
     * @brief Reads the name of the global instance MO2 keeps as current in the registry, empty when there is none.
     */
    static std::wstring mo2CurrentInstanceName();
};
