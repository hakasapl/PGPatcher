#pragma once
#include "common/BethesdaGame.hpp"
#include "util/FileUtil.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <bsa/tes4.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

constexpr unsigned asciiUpperBound = 127;

/**
 * @brief Case-insensitive map of every file in a Bethesda data folder, loose or archived, as the game would resolve it.
 *
 * Only the folders in foldersToMap and the top level of the data folder are mapped. A loose file always wins over an
 * archived one, and a later archive wins over an earlier one. Files written by PGPatcher can be registered as
 * "generated" files, which shadow the original entry until clearGeneratedFiles() restores it. Keys are lower case
 * paths relative to the data folder.
 */
class BethesdaDirectory {
public:
    /**
     * @struct FileIdentity
     * @brief Cheap, content-free description of where a file in the load order comes from and when it last changed.
     *
     * Two identities compare equal when the file almost certainly has the same content: for loose files the
     * modification time and size must match, for BSA-packed files the containing archive's name, modification time
     * and size must match. This is what incremental runs use to decide whether a source file changed without reading
     * it.
     */
    struct FileIdentity {
        enum class Kind : uint8_t { None, Loose, BSA, Generated };

        Kind kind = Kind::None;
        int64_t mtime = 0; /**< Loose files: last write time (file_time_type ticks) */
        uint64_t size = 0; /**< Loose files: size in bytes */
        std::wstring bsaRelPath; /**< BSA files: lowercase archive name */
        int64_t bsaMtime = 0; /**< BSA files: archive last write time (file_time_type ticks) */
        uint64_t bsaSize = 0; /**< BSA files: archive size in bytes */

        bool operator==(const FileIdentity& other) const = default;
    };

    /**
     * @brief Observer interface for file map queries. Set per thread; used to record what a computation depended on.
     */
    class FileQueryObserver {
    public:
        virtual ~FileQueryObserver() = default;
        FileQueryObserver() = default;
        FileQueryObserver(const FileQueryObserver&) = default;
        FileQueryObserver& operator=(const FileQueryObserver&) = default;
        FileQueryObserver(FileQueryObserver&&) = default;
        FileQueryObserver& operator=(FileQueryObserver&&) = default;

        /**
         * @brief Called whenever isFile() is answered on the observing thread.
         */
        virtual void onIsFile(const std::filesystem::path& relPath,
                              bool exists,
                              bool isGenerated) = 0;

        /**
         * @brief Called whenever file() successfully resolves a file on the observing thread.
         */
        virtual void onGetFile(const std::filesystem::path& relPath,
                               const FileIdentity& identity) = 0;
    };

    /**
     * @brief Reads a mapped file (lower case path relative to the data folder) in place of the directory's own loose
     * file and archive reading, see setFileReader().
     */
    using FileReader = std::function<std::vector<std::byte>(const std::filesystem::path&)>;

    /**
     * @brief Thrown by a reader (setFileReader()) for a mapped file that could not be read this time, as opposed to one
     * that is not there: the lookups that catch it treat the read as one that may succeed later.
     */
    class ReadFailure : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    /**
     * @struct LooseFile
     * @brief A loose file of the data folder as a listing of it reports the file, for a LooseFileSource.
     */
    struct LooseFile {
        std::wstring relPath; /**< Path relative to the data folder, in the case of the listing */
        int64_t mtime = 0; /**< Last write time (file_time_type ticks) */
        uint64_t size = 0; /**< Size in bytes */
        bool isHidden = false; /**< Hidden as isHidden() reports it: the attribute, a dot name or .mohidden */
        bool isInHiddenFolder = false; /**< Below a folder that is hidden the same way */
    };

    /**
     * @brief Supplies the loose files of the data folder in place of listing it, see setLooseFileSource().
     */
    class LooseFileSource {
    public:
        virtual ~LooseFileSource() = default;
        LooseFileSource() = default;
        LooseFileSource(const LooseFileSource&) = default;
        LooseFileSource& operator=(const LooseFileSource&) = default;
        LooseFileSource(LooseFileSource&&) = default;
        LooseFileSource& operator=(LooseFileSource&&) = default;

        /**
         * @brief Calls the visitor for every loose file below the data folder, in any order. Files outside the top
         * level and the folders to map may be among them, the directory leaves them out.
         */
        virtual void forEachLooseFile(const std::function<void(const LooseFile&)>& visitor) const = 0;
    };

private:
    /**
     * @struct BSAFile
     * @brief An opened BSA archive, shared by every file map entry that comes from it.
     *
     * path is the archive's full path in the original case, relPath its lower case file name, version the archive
     * format version and archive the parsed archive the files are read from.
     */
    struct BSAFile {
        BSAFile(std::filesystem::path p,
                std::filesystem::path rp,
                bsa::tes4::version v,
                bsa::tes4::archive a,
                int64_t mt,
                uint64_t sz)
            : path(std::move(p))
            , relPath(std::move(rp))
            , version(v)
            , archive(std::move(a))
            , mtime(mt)
            , size(sz)
        {
        }

        std::filesystem::path path;
        std::filesystem::path relPath;
        bsa::tes4::version version;
        bsa::tes4::archive archive;
        int64_t mtime; /**< Archive last write time (file_time_type ticks) */
        uint64_t size; /**< Archive size in bytes */
    };

    /**
     * @struct BethesdaFile
     * @brief One entry of the file map.
     *
     * path is the lower case path relative to the data folder (empty for a lookup miss), bsaFile the archive the file
     * comes from or nullptr for a loose or generated file.
     */
    struct BethesdaFile {
        std::filesystem::path path;
        std::shared_ptr<BSAFile> bsaFile;
        bool isGenerated = false;
        int64_t mtime = 0; /**< Loose files: last write time (file_time_type ticks) */
        uint64_t size = 0; /**< Loose files: size in bytes */

        [[nodiscard]] nlohmann::json diagJSON() const
        {
            auto j = nlohmann::json::object();

            if (bsaFile)
                j["bsa"] = StringUtil::utf16toUTF8(bsaFile->path.wstring());

            return j;
        }
    };

    // Class member variables.
    std::filesystem::path m_dataDir; /**< Stores the path to the game data directory */
    std::filesystem::path m_generatedDir; /**< Stores the path to the generated directory */
    std::map<std::filesystem::path, BethesdaFile> m_fileMap; /** < Stores the file map for every file found in the load
                                                              order. Key is a lowercase path, value is a BethesdaFile*/
    std::map<std::filesystem::path, BethesdaFile>
        m_generatedFileRestoreMap; /**< Original file-map entries that were
                                                                                                              overridden
                                      by generated files and can be restored when generated files are deleted
                                                                                                          */
    std::shared_mutex m_fileMapMutex; /** < Shared Mutex for the file map */
    std::unordered_set<std::filesystem::path>
        m_foldersToMap; /**< Set of folders to include when populating the file map, all lowercase */
    std::unordered_set<std::filesystem::path> m_excludedFiles; /**< Files populateFileMap() leaves out, lower case */

    BethesdaGame* m_bg; /** < BethesdaGame which stores a BethesdaGame object
                        corresponding to this load order */

    FileReader m_fileReader; /**< When set, file() reads every mapped file through it */
    std::optional<std::vector<std::wstring>> m_archiveLoadOrder; /**< Replaces the derived archive load order */
    bool m_shouldSkipHiddenFiles = true; /**< Whether hidden loose files stay out of the file map */
    const LooseFileSource* m_looseFileSource = nullptr; /**< Supplies the loose files instead of a listing */

    inline thread_local static FileQueryObserver* s_threadQueryObserver = nullptr;

    /**
     * @brief The INI keys of the [Archive] section that list archives to load (sResourceArchiveList and friends).
     */
    static std::vector<std::string> iniBSAFields();

    /**
     * @brief Builds the identity for a file map entry
     */
    static FileIdentity buildIdentity(const BethesdaFile& file);

    /**
     * @brief Extensions that are never mapped (archives and plugins), lower case.
     */
    static const std::vector<std::wstring>& extensionBlocklist();

public:
    /**
     * @brief Constructs a directory for a game's data folder, so that archives can be mapped too.
     *
     * @param bg Game whose data folder, INI files and plugin load order are used. Must outlive this object.
     * @param foldersToMap Lower case top level folders of the data folder to map (e.g. "meshes", "textures").
     * @param generatedPath Output folder that generated files are read from and written to.
     * @throws std::invalid_argument if bg is null.
     */
    BethesdaDirectory(BethesdaGame* bg,
                      std::unordered_set<std::filesystem::path> foldersToMap,
                      std::filesystem::path generatedPath = "");

    /**
     * @brief Constructs a directory for a plain folder without a game. Only loose files can be mapped.
     *
     * @param dataPath Folder to treat as the data folder.
     * @param foldersToMap Lower case top level folders of dataPath to map (e.g. "meshes", "textures").
     * @param generatedPath Output folder that generated files are read from and written to.
     */
    BethesdaDirectory(std::filesystem::path dataPath,
                      std::unordered_set<std::filesystem::path> foldersToMap,
                      std::filesystem::path generatedPath = "");

    /**
     * @brief Clears and repopulates the file map with every file of the load order.
     *
     * @param includeBSAs Whether files in BSA archives are mapped as well. Ignored when there is no BethesdaGame.
     * @param shouldMultithread Whether the archives are read on a second thread while the loose files are mapped.
     * @throws std::filesystem::filesystem_error if the data folder cannot be listed. An exception thrown while reading
     * the archives is rethrown on the calling thread.
     */
    void populateFileMap(bool includeBSAs = true,
                         bool shouldMultithread = true);

    /**
     * @brief Returns the file map. Keys are lower case paths relative to the data folder.
     */
    [[nodiscard]] const std::map<std::filesystem::path,
                                 BethesdaFile>&
    fileMap() const;

    /**
     * @brief Get the data directory path
     *
     * @return std::filesystem::path absolute path to the data directory
     */
    [[nodiscard]] std::filesystem::path dataPath() const;

    /**
     * @brief Get the Generated Path
     *
     * @return std::filesystem::path absolute path to the generated directory
     */
    [[nodiscard]] std::filesystem::path generatedPath() const;

    /**
     * @brief Reads a file of the load order, from the data folder, the output folder (generated files) or its archive.
     *
     * The read is reported to the thread's FileQueryObserver.
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return the file contents, or an empty vector if an archived file could not be extracted
     * @throws std::runtime_error if the file is not in the file map or not in its archive
     */
    [[nodiscard]] std::vector<std::byte> file(const std::filesystem::path& relPath);

    /**
     * @brief Maps a file written to the output folder, shadowing any entry of the load order at the same path.
     *
     * The shadowed entry is kept so that clearGeneratedFiles() can restore it.
     *
     * @param relPath lower case path of the generated file relative to the output folder
     */
    void addGeneratedFile(const std::filesystem::path& relPath);

    /**
     * @brief Removes every generated file from the file map, restoring the load order entries they shadowed.
     *
     * Called before a patch run so files generated by a previous run are not taken for inputs.
     */
    void clearGeneratedFiles();

    /**
     * @brief Checks whether a file of the load order is a loose file (generated files count as loose).
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return true if the file is mapped and does not come from an archive
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] bool isLooseFile(const std::filesystem::path& relPath);

    /**
     * @brief Checks whether a file of the load order comes from a BSA archive.
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return true if the file is mapped and comes from an archive
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] bool isBSAFile(const std::filesystem::path& relPath);

    /**
     * @brief Checks whether a file exists in the load order. The query is reported to the thread's FileQueryObserver.
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return true if the file is mapped
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] bool isFile(const std::filesystem::path& relPath);

    /**
     * @brief Checks whether a file of the load order was registered with addGeneratedFile().
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return true if the file is mapped as a generated file
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] bool isGenerated(const std::filesystem::path& relPath);

    /**
     * @brief Returns the full path of a loose file: in the output folder for a generated file, in the data folder for
     * everything else (whether or not the file is mapped).
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return absolute path to the file
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] std::filesystem::path looseFileFullPath(const std::filesystem::path& relPath);

    /**
     * @brief Returns the identity (source, modification time and size) of a file in the load order without reading it.
     *
     * @param relPath lower case path to the file relative to the data directory
     * @return the identity, with kind Kind::None if the file is not in the load order
     */
    [[nodiscard]] FileIdentity fileIdentity(const std::filesystem::path& relPath);

    /**
     * @brief Sets (or clears with nullptr) the file query observer for the calling thread
     *
     * @param observer observer to notify of file map queries made on this thread
     */
    static void setThreadFileQueryObserver(FileQueryObserver* observer);

    /**
     * @brief Makes file() read every mapped file through the given function instead of the data folder and the
     * archives. The PGLite runtime reads through the game's resource layer this way. An empty function restores the
     * directory's own reading. Not thread safe: no file() may run concurrently.
     *
     * @param reader Reader of a mapped file.
     */
    void setFileReader(FileReader reader);

    /**
     * @brief Takes the loose files from the source instead of listing the data folder, which usvfs merges file by
     * file when the process runs under Mod Organizer 2 (MO2DataFolder). The hidden file rule, the folders to map, the
     * extension block list and the excluded files apply to them as to a listing. nullptr restores the listing. Call
     * before populateFileMap(); the source has to outlive the call.
     *
     * @param source Source of the loose files, or nullptr.
     */
    void setLooseFileSource(const LooseFileSource* source);

    /**
     * @brief Replaces the archive load order bsaLoadOrder() derives from the INI files and the plugins, so archives
     * can be mapped without a BethesdaGame. Call before populateFileMap().
     *
     * @param archives Archive file names in the data folder, the first element loads first.
     */
    void setArchiveLoadOrder(std::vector<std::wstring> archives);

    /**
     * @brief Frees the directory tables of the mapped archives. The file map keeps which archive a file comes from,
     * but archived files can only be read through a file reader afterwards. Not thread safe: no file() may run
     * concurrently.
     */
    void dropArchiveContents();

    /**
     * @brief Sets whether hidden loose files (hidden attribute, dot files, .mohidden files) stay out of the file map.
     * They do by default; the game itself loads them. Call before populateFileMap().
     */
    void setShouldSkipHiddenFiles(bool shouldSkip);

    /**
     * @brief Replaces the lower case top level folders of the data folder that populateFileMap() maps. Call before
     * populateFileMap().
     */
    void setFoldersToMap(std::unordered_set<std::filesystem::path> foldersToMap);

    /**
     * @brief Files populateFileMap() leaves out of the map, loose or archived, as if the load order did not have
     * them: lower case paths relative to the data folder. The PGLite runtime keeps the textures an enabled offline
     * output generated out of its inputs this way. Call before populateFileMap().
     */
    void setExcludedFiles(std::unordered_set<std::filesystem::path> files);

    /**
     * @brief Returns the archives of the load order in load order: first the archives listed in the INI files, then the
     * archives that belong to each active plugin, or the order given to setArchiveLoadOrder(). Archives in the data
     * folder that nothing loads are logged as warnings.
     *
     * @return Archive file names in load order, the first element loads first.
     * @throws std::runtime_error if there is no BethesdaGame and no injected order.
     */
    [[nodiscard]] std::vector<std::wstring> bsaLoadOrder() const;

    /**
     * @brief Returns the path that identifies which mod a file comes from: the archive's lower case file name for an
     * archived file, the path itself otherwise.
     *
     * @param relPath lower case path to the file relative to the data directory
     */
    [[nodiscard]] std::filesystem::path modLookupFile(const std::filesystem::path& relPath);

    // Helpers.

    /**
     * @brief Checks if fs::path object has only ascii characters
     *
     * @param path Path to check
     * @return true When path only has ascii chars
     * @return false When path has other than ascii chars
     */
    [[nodiscard]] static bool isPathAscii(const std::filesystem::path& path);

    /**
     * @brief Checks whether a file of the load order comes from one of the given archives.
     *
     * @param file lower case path to the file relative to the data directory
     * @param bsaFiles Archive file names to check against, compared without regard to case
     * @return true if the file is archived and its archive is in the list
     * @throws std::runtime_error if the file map was not populated
     */
    [[nodiscard]] bool isFileInBSA(const std::filesystem::path& file,
                                   const std::vector<std::wstring>& bsaFiles);

    /**
     * @brief Check if any glob in list matches string. Globs are basic MS-DOS wildcards, a * represents any number of
     * any character including slashes
     *
     * @param str String to check
     * @param globList Globs to check
     * @return true if any match
     * @return false if none match
     */
    static bool checkGlob(const std::wstring& str,
                          const std::vector<std::wstring>& globList);

    /**
     * @brief Checks whether a directory entry is hidden: it has the hidden attribute, its name starts with a dot, or it
     * ends with .mohidden (a file hidden in Mod Organizer 2).
     *
     * @param entry Entry of a directory listing to check.
     * @return true if the entry is hidden
     */
    static bool isHidden(const FileUtil::DirectoryEntry& entry);

private:
    /**
     * @brief Adds the files of every archive of bsaLoadOrder() to the file map. Requires a BethesdaGame.
     */
    void addBSAFilesToMap();

    /**
     * @brief Adds the loose files of the data folder's top level and of the folders to map to the file map, skipping
     * hidden entries and blocked extensions.
     */
    void addLooseFilesToMap();

    /**
     * @brief Adds the files of one archive to the file map. Files outside the folders to map and files with non-ASCII
     * names are skipped, a missing or unreadable archive is logged.
     *
     * @param bsaName Archive file name in the data folder
     */
    void addBSAToFileMap(const std::wstring& bsaName);

    /**
     * @brief Checks whether a path relative to the data folder is one populateFileMap() maps: at the top level of the
     * data folder or below one of the folders to map.
     *
     * @param relPath Path relative to the data folder, in any case.
     */
    [[nodiscard]] bool isMappedPath(const std::wstring& relPath) const;

    /**
     * @brief Checks whether a file may enter the file map, which only its extension decides (see extensionBlocklist()).
     *
     * @param filePath File being checked
     * @return true if file should be added
     */
    static bool isFileAllowed(const std::filesystem::path& filePath);

    /**
     * @brief Returns the archives listed in the INI files, in the order the INI keys list them. The game's INI files
     * are read first and INI files in the data folder override them key by key.
     */
    [[nodiscard]] std::vector<std::wstring> bsaFilesFromINIs() const;

    /**
     * @brief Returns the file names of the archives in the data folder.
     */
    [[nodiscard]] std::vector<std::wstring> bsaFilesInDirectory() const;

    /**
     * @brief Returns the archives a plugin loads: "<plugin>.bsa" first, then "<plugin><digits>.bsa" and
     * "<plugin> - <suffix>.bsa" in the order given.
     *
     * @param bsaFileList Archive file names to pick from
     * @param pluginPrefix Plugin file name without extension
     */
    [[nodiscard]] static std::vector<std::wstring>
    findBSAFilesFromPluginName(const std::vector<std::wstring>& bsaFileList,
                               const std::wstring& pluginPrefix);

    /**
     * @brief Looks a file up in the file map.
     *
     * @param filePath lower case path to the file relative to the data directory
     * @return the entry, or an entry with an empty path if the file is not mapped
     */
    [[nodiscard]] BethesdaFile fileFromMap(const std::filesystem::path& filePath);

    /**
     * @brief Adds a file to the file map or replaces its entry.
     *
     * A loose file replaces whatever is mapped for the path. An archived file replaces the file of an earlier archive
     * but never a loose file, so loose files and archives can be added in either order.
     *
     * @param filePath lower case path to update or add
     * @param bsaFile archive the file comes from, or nullptr for a loose file
     * @param isGenerated whether the file is a generated file
     * @param mtime loose file last write time (file_time_type ticks), 0 if unknown
     * @param size loose file size in bytes, 0 if unknown
     */
    void updateFileMap(const std::filesystem::path& filePath,
                       std::shared_ptr<BSAFile> bsaFile,
                       const bool& isGenerated = false,
                       const int64_t& mtime = 0,
                       const uint64_t& size = 0);

    /**
     * @brief Convert a list of wstrings to a LPCWSTRs
     *
     * @param original original list of wstrings to convert
     * @return std::vector<LPCWSTR> list of LPCWSTRs
     */
    [[nodiscard]] static std::vector<LPCWSTR> convertWStringToLPCWSTRVector(const std::vector<std::wstring>& original);

    /**
     * @brief Checks whether any glob in the list matches a string, trying the glob that matched last time first.
     *
     * @param str String to check
     * @param[in,out] winningGlob Glob that matched the previous call (updated when another glob matches)
     * @param globList List of globs to check
     * @return true if any glob matched
     */
    static bool checkGlob(const LPCWSTR& str,
                          LPCWSTR& winningGlob,
                          const std::vector<LPCWSTR>& globList);

    /**
     * @brief Reads one value from an INI file. Section and key are compared without regard to case, ';' and '#' start
     * comment lines.
     *
     * @return the trimmed value, or an empty string if the file, section or key does not exist
     */
    static std::wstring readINIValue(const std::filesystem::path& iniPath,
                                     const std::wstring& section,
                                     const std::wstring& key);
};
