#pragma once
#include "common/BethesdaGame.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <bsa/tes4.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

constexpr unsigned asciiUpperBound = 127;

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
        enum class Kind : uint8_t { None, LOOSE, BSA, GENERATED };

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

private:
    /**
     * @struct BSAFile
     * @brief Stores data about an individual BSA file
     *
     * path stores the path to the BSA archive, preserving case from the original
     * path version stores the version of the BSA archive archive stores the BSA
     * archive object, which is where files can be accessed
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
     * @brief Structure which holds information about a specific file in the file
     * map
     *
     * path stores the path to the file, preserving case from the original path
     * bsa_file stores a shared pointer to a BSA file struct, or nullptr if the
     * file is a loose file
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

    BethesdaGame* m_bg; /** < BethesdaGame which stores a BethesdaGame object
                        corresponding to this load order */

    inline thread_local static FileQueryObserver* s_threadQueryObserver = nullptr;

    /**
     * @brief Returns a vector of strings that represent the fields in the INI
     * file that store information about BSA file loading
     *
     * @return std::vector<std::string>
     */
    static std::vector<std::string> inibsaFields();

    /**
     * @brief Builds the identity for a file map entry
     */
    static FileIdentity buildIdentity(const BethesdaFile& file);
    /**
     * @brief Gets a list of extensions to ignore when populating the file map
     *
     * @return std::vector<std::wstring>
     */
    static std::vector<std::wstring> extensionBlocklist();

public:
    /**
     * @brief Construct a new Bethesda Directory object
     *
     * @param bg BethesdaGame object corresponding to load order
     * @param logging Whether to enable CLI logging
     */
    BethesdaDirectory(BethesdaGame* bg,
                      std::unordered_set<std::filesystem::path> foldersToMap,
                      std::filesystem::path generatedPath = "");

    /**
     * @brief Construct a new Bethesda Directory object without a game type, for generic folders only
     *
     * @param dataPath Data path
     * @param generatedPath Generated path
     * @param pgmm PGModManager object
     * @param logging Whether to enable CLI logging
     */
    BethesdaDirectory(std::filesystem::path dataPath,
                      std::unordered_set<std::filesystem::path> foldersToMap,
                      std::filesystem::path generatedPath = "");

    /**
     * @brief Populate file map with all files in the load order
     */
    void populateFileMap(bool includeBSAs = true);

    /**
     * @brief Get the file map vector, path of the files is is all lower case
     *
     * @return std::map<std::filesystem::path, BethesdaFile>
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
     * @brief Get bytes from a file in the load order. Throws runtime_error if file does not exist and logging is turned
     * off
     *
     * @param relPath path to the file relative to the data directory
     * @return std::vector<std::byte> vector of bytes of the file
     */
    [[nodiscard]] std::vector<std::byte> file(const std::filesystem::path& relPath);

    /**
     * @brief Create a Generated file in the file map
     *
     * @param relPath path of the generated file
     */
    void addGeneratedFile(const std::filesystem::path& relPath);

    /**
     * @brief Remove all generated-file entries from the in-memory file map.
     *
     * This is useful before a fresh patch run so generated files from a
     * previous run do not remain marked as active inputs.
     */
    void clearGeneratedFiles();

    /**
     * @brief Check if a file in the load order is a loose file
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a loose file
     * @return false if file is not a loose file or doesn't exist
     */
    [[nodiscard]] bool isLooseFile(const std::filesystem::path& relPath);

    /**
     * @brief Check if a file in the load order is a file from a BSA
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a BSA file
     * @return false if file is not a BSA file or doesn't exist
     */
    [[nodiscard]] bool isBSAFile(const std::filesystem::path& relPath);

    /**
     * @brief Check if a file exists in the load order
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file exists in the load order
     * @return false if file does not exist in the load order
     */
    [[nodiscard]] bool isFile(const std::filesystem::path& relPath);

    /**
     * @brief Check if a file is a generated file
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is generated
     * @return false if file is not generated
     */
    [[nodiscard]] bool isGenerated(const std::filesystem::path& relPath);

    /**
     * @brief Check if file is a directory
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a directory
     * @return false if file is not a directory or doesn't exist
     */
    [[nodiscard]] bool isPrefix(const std::filesystem::path& relPath);

    /**
     * @brief Get the full path of a loose file in the load order
     *
     * @param relPath path to the file relative to the data directory
     * @return std::filesystem::path absolute path to the file
     */
    [[nodiscard]] std::filesystem::path looseFileFullPath(const std::filesystem::path& relPath);

    /**
     * @brief Get the identity (source + modification time + size) of a file in the load order without reading it
     *
     * @param relPath path to the file relative to the data directory
     * @return FileIdentity identity, kind is NONE if the file does not exist in the load order
     */
    [[nodiscard]] FileIdentity fileIdentity(const std::filesystem::path& relPath);

    /**
     * @brief Sets (or clears with nullptr) the file query observer for the calling thread
     *
     * @param observer observer to notify of file map queries made on this thread
     */
    static void setThreadFileQueryObserver(FileQueryObserver* observer);

    /**
     * @brief Get the load order of BSAs
     *
     * @return std::vector<std::wstring> Names of BSA files ordered by load order.
     * First element is loaded first.
     */
    [[nodiscard]] std::vector<std::wstring> bsaLoadOrder() const;

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
     * @brief Check if a file is in a list of BSA files
     *
     * @param file File to check
     * @param bsaFiles List of BSA files to check against
     */
    [[nodiscard]] bool isFileInBSA(const std::filesystem::path& file,
                                   const std::vector<std::wstring>& bsaFiles);

    /**
     * @brief Check if any component on a path is in a list of components
     *
     * @param path path to check
     * @param components components to check against
     * @return true if any component is in the path
     * @return false if no component is in the path
     */
    static bool checkIfAnyComponentIs(const std::filesystem::path& path,
                                      const std::vector<std::wstring>& components);

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
     * @brief Check if file or folder is hidden
     *
     * @param path Path to check
     * @return true if file is hidden
     * @return false if file is not hidden
     */
    static bool isHidden(const std::filesystem::path& path);

private:
    /**
     * @brief Looks through each BSA and adds files to the file map
     */
    void addBSAFilesToMap();

    /**
     * @brief Looks through all loose files in the load order and adds to the file
     * map
     */
    void addLooseFilesToMap();

    /**
     * @brief Add files in a BSA to the file map
     *
     * @param bsaName BSA name to read files from
     */
    void addBSAToFileMap(const std::wstring& bsaName);

    /**
     * @brief Check if a file being added to the file map should be added
     *
     * @param filePath File being checked
     * @return true if file should be added
     * @return false if file should not be added
     */
    static bool isFileAllowed(const std::filesystem::path& filePath);

    /**
     * @brief Get BSA files defined in INI files
     *
     * @return std::vector<std::wstring> list of BSAs in the order the INI has
     * them
     */
    [[nodiscard]] std::vector<std::wstring> bsaFilesFromINIs() const;

    /**
     * @brief Get BSA files in the data directory
     *
     * @return std::vector<std::wstring> list of BSAs in the data directory
     */
    [[nodiscard]] std::vector<std::wstring> bsaFilesInDirectory() const;

    /**
     * @brief gets BSA files that are loaded with a plugin
     *
     * @param bsaFileList List of BSA files to check
     * @param pluginPrefix Plugin to check without extension
     * @return std::vector<std::wstring> list of BSAs loaded with plugin
     */
    [[nodiscard]] static std::vector<std::wstring>
    findBSAFilesFromPluginName(const std::vector<std::wstring>& bsaFileList,
                               const std::wstring& pluginPrefix);

    /**
     * @brief Get a file object from the file map
     *
     * @param filePath Path to get the object for
     * @return BethesdaFile object of file in load order
     */
    [[nodiscard]] BethesdaFile fileFromMap(const std::filesystem::path& filePath);

    /**
     * @brief Update the file map with
     *
     * @param filePath path to update or add
     * @param bsaFile BSA file or nullptr if it doesn't exist
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
     * @brief Checks whether a glob matches a provided list of globs
     *
     * @param str String to check
     * @param winningGlob Last glob that one (for performance)
     * @param globList List of globs to check
     * @return true Any glob is the list mated
     * @return false No globs in the list matched
     */
    static bool checkGlob(const LPCWSTR& str,
                          LPCWSTR& winningGlob,
                          const std::vector<LPCWSTR>& globList);

    static std::wstring readINIValue(const std::filesystem::path& iniPath,
                                     const std::wstring& section,
                                     const std::wstring& key);
};
