#pragma once
#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "ParallaxGenUtil.hpp"

#include <nlohmann/json.hpp>

#include <bsa/tes4.hpp>

#include <boost/algorithm/string.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

constexpr unsigned ASCII_UPPER_BOUND = 127;

class BethesdaDirectory {
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
        std::filesystem::path path;
        bsa::tes4::version version;
        bsa::tes4::archive archive;
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
        std::shared_ptr<ModManagerDirectory::Mod> mod;
        bool generated;

        [[nodiscard]] auto getDiagJSON() const -> nlohmann::json
        {
            auto j = nlohmann::json::object();
            if (mod != nullptr) {
                j["mod"] = ParallaxGenUtil::utf16toUTF8(mod->name);
            }

            if (bsaFile != nullptr) {
                j["bsa"] = ParallaxGenUtil::utf16toUTF8(bsaFile->path.wstring());
            }

            return j;
        }
    };

    /**
     * @struct ModFile
     * @brief Structure which the path and the file size for a specific mod file
     *
     * Path stores the path to the file, preserving case from the original path
     * FileSize stores the size of the file in bytes
     * CRC32 stores the CRC32 checksum of the file
     */
    struct ModFile {
        std::filesystem::path path;
        uintmax_t fileSize;
        unsigned int crc32;
    };

    // Class member variables
    std::filesystem::path m_dataDir; /**< Stores the path to the game data directory */
    std::filesystem::path m_generatedDir; /**< Stores the path to the generated directory */
    std::map<std::filesystem::path, BethesdaFile> m_fileMap; /** < Stores the file map for every file found in the load
                                                              order. Key is a lowercase path, value is a BethesdaFile*/
    std::mutex m_fileMapMutex; /** < Mutex for the file map */
    std::vector<ModFile> m_modFiles; /** < Stores files in mod staging directory */

    std::unordered_map<std::filesystem::path, std::vector<std::byte>> m_fileCache; /** < Stores a cache of file bytes */
    std::mutex m_fileCacheMutex; /** < Mutex for the file cache map */

    bool m_logging; /** < Bool for whether logging is enabled or not */
    BethesdaGame* m_bg; /** < BethesdaGame which stores a BethesdaGame object
                        corresponding to this load order */
    ModManagerDirectory* m_mmd; /** < ModManagerDirectory which stores a pointer to a
                                 ModManagerDirectory object corresponding to this
                                 load order */

    /**
     * @brief Returns a vector of strings that represent the fields in the INI
     * file that store information about BSA file loading
     *
     * @return std::vector<std::string>
     */
    static auto getINIBSAFields() -> std::vector<std::string>;
    /**
     * @brief Gets a list of extensions to ignore when populating the file map
     *
     * @return std::vector<std::wstring>
     */
    static auto getExtensionBlocklist() -> std::vector<std::wstring>;

public:
    /**
     * @brief Construct a new Bethesda Directory object
     *
     * @param bg BethesdaGame object corresponding to load order
     * @param logging Whether to enable CLI logging
     */
    BethesdaDirectory(BethesdaGame* bg, std::filesystem::path generatedPath = "", ModManagerDirectory* mmd = nullptr,
        const bool& logging = false);

    /**
     * @brief Construct a new Bethesda Directory object without a game type, for generic folders only
     *
     * @param dataPath Data path
     * @param generatedPath Generated path
     * @param mmd ModManagerDirectory object
     * @param logging Whether to enable CLI logging
     */
    BethesdaDirectory(std::filesystem::path dataPath, std::filesystem::path generatedPath = "",
        ModManagerDirectory* mmd = nullptr, const bool& logging = false);

    /**
     * @brief Populate file map with all files in the load order
     */
    void populateFileMap(bool includeBSAs = true);

    /**
     * @brief Get the file map vector, path of the files is is all lower case
     *
     * @return std::map<std::filesystem::path, BethesdaFile>
     */
    [[nodiscard]] auto getFileMap() const -> const std::map<std::filesystem::path, BethesdaFile>&;

    /**
     * @brief Get the data directory path
     *
     * @return std::filesystem::path absolute path to the data directory
     */
    [[nodiscard]] auto getDataPath() const -> std::filesystem::path;

    /**
     * @brief Get the Generated Path
     *
     * @return std::filesystem::path absolute path to the generated directory
     */
    [[nodiscard]] auto getGeneratedPath() const -> std::filesystem::path;

    /**
     * @brief Get bytes from a file in the load order. Throws runtime_error if file does not exist and logging is turned
     * off
     *
     * @param relPath path to the file relative to the data directory
     * @return std::vector<std::byte> vector of bytes of the file
     */
    [[nodiscard]] auto getFile(const std::filesystem::path& relPath, const bool& cacheFile = false)
        -> std::vector<std::byte>;

    /**
     * @brief Get the Mod that has the winning version of the file
     *
     * @param relPath path to the file relative to the data directory
     * @return std::wstring mod label
     */
    [[nodiscard]] auto getMod(const std::filesystem::path& relPath) -> std::shared_ptr<ModManagerDirectory::Mod>;

    /**
     * @brief Create a Generated file in the file map
     *
     * @param relPath path of the generated file
     * @param mod wstring mod label to assign
     */
    void addGeneratedFile(const std::filesystem::path& relPath, std::shared_ptr<ModManagerDirectory::Mod> mod);

    /**
     * @brief Clear the file cache
     */
    auto clearCache() -> void;

    /**
     * @brief Check if a file in the load order is a loose file
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a loose file
     * @return false if file is not a loose file or doesn't exist
     */
    [[nodiscard]] auto isLooseFile(const std::filesystem::path& relPath) -> bool;

    /**
     * @brief Check if a file in the load order is a file from a BSA
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a BSA file
     * @return false if file is not a BSA file or doesn't exist
     */
    [[nodiscard]] auto isBSAFile(const std::filesystem::path& relPath) -> bool;

    /**
     * @brief Check if a file exists in the load order
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file exists in the load order
     * @return false if file does not exist in the load order
     */
    [[nodiscard]] auto isFile(const std::filesystem::path& relPath) -> bool;

    /**
     * @brief Check if a file is a generated file
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is generated
     * @return false if file is not generated
     */
    [[nodiscard]] auto isGenerated(const std::filesystem::path& relPath) -> bool;

    /**
     * @brief Check if file is a directory
     *
     * @param relPath path to the file relative to the data directory
     * @return true if file is a directory
     * @return false if file is not a directory or doesn't exist
     */
    [[nodiscard]] auto isPrefix(const std::filesystem::path& relPath) -> bool;

    /**
     * @brief Get the full path of a loose file in the load order
     *
     * @param relPath path to the file relative to the data directory
     * @return std::filesystem::path absolute path to the file
     */
    [[nodiscard]] auto getLooseFileFullPath(const std::filesystem::path& relPath) -> std::filesystem::path;

    /**
     * @brief Get the load order of BSAs
     *
     * @return std::vector<std::wstring> Names of BSA files ordered by load order.
     * First element is loaded first.
     */
    [[nodiscard]] auto getBSALoadOrder() const -> std::vector<std::wstring>;

    // Helpers

    /**
     * @brief Checks if fs::path object has only ascii characters
     *
     * @param path Path to check
     * @return true When path only has ascii chars
     * @return false When path has other than ascii chars
     */
    [[nodiscard]] static auto isPathAscii(const std::filesystem::path& path) -> bool;

    /**
     * @brief Check if a file is in a list of BSA files
     *
     * @param file File to check
     * @param bsaFiles List of BSA files to check against
     */
    [[nodiscard]] auto isFileInBSA(const std::filesystem::path& file, const std::vector<std::wstring>& bsaFiles)
        -> bool;

    /**
     * @brief Get the lowercase path of a path using the "C" locale, i.e. only ASCII characters are converted
     *
     * @param path Path to be made lowercase
     * @return std::filesystem::path lowercase path of the input
     */
    static auto getAsciiPathLower(const std::filesystem::path& path) -> std::filesystem::path;

    /**
     * @brief Check if two paths are equal, ignoring case
     *
     * @param path1 1st path to check
     * @param path2 2nd path to check
     * @return true if paths equal ignoring case
     * @return false if paths don't equal ignoring case
     */
    static auto pathEqualityIgnoreCase(const std::filesystem::path& path1, const std::filesystem::path& path2) -> bool;

    /**
     * @brief Check if any component on a path is in a list of components
     *
     * @param path path to check
     * @param components components to check against
     * @return true if any component is in the path
     * @return false if no component is in the path
     */
    static auto checkIfAnyComponentIs(const std::filesystem::path& path, const std::vector<std::wstring>& components)
        -> bool;

    /**
     * @brief Check if any glob in list matches string. Globs are basic MS-DOS wildcards, a * represents any number of
     * any character including slashes
     *
     * @param str String to check
     * @param globList Globs to check
     * @return true if any match
     * @return false if none match
     */
    static auto checkGlob(const std::wstring& str, const std::vector<std::wstring>& globList) -> bool;

    /**
     * @brief Check if file or folder is hidden
     *
     * @param path Path to check
     * @return true if file is hidden
     * @return false if file is not hidden
     */
    static auto isHidden(const std::filesystem::path& path) -> bool;

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
    static auto isFileAllowed(const std::filesystem::path& filePath) -> bool;

    /**
     * @brief Get BSA files defined in INI files
     *
     * @return std::vector<std::wstring> list of BSAs in the order the INI has
     * them
     */
    [[nodiscard]] auto getBSAFilesFromINIs() const -> std::vector<std::wstring>;

    /**
     * @brief Get BSA files in the data directory
     *
     * @return std::vector<std::wstring> list of BSAs in the data directory
     */
    [[nodiscard]] auto getBSAFilesInDirectory() const -> std::vector<std::wstring>;

    /**
     * @brief gets BSA files that are loaded with a plugin
     *
     * @param bsaFileList List of BSA files to check
     * @param pluginPrefix Plugin to check without extension
     * @return std::vector<std::wstring> list of BSAs loaded with plugin
     */
    [[nodiscard]] auto findBSAFilesFromPluginName(const std::vector<std::wstring>& bsaFileList,
        const std::wstring& pluginPrefix) const -> std::vector<std::wstring>;

    /**
     * @brief Get a file object from the file map
     *
     * @param filePath Path to get the object for
     * @return BethesdaFile object of file in load order
     */
    [[nodiscard]] auto getFileFromMap(const std::filesystem::path& filePath) -> BethesdaFile;

    /**
     * @brief Update the file map with
     *
     * @param filePath path to update or add
     * @param bsaFile BSA file or nullptr if it doesn't exist
     */
    void updateFileMap(const std::filesystem::path& filePath, std::shared_ptr<BSAFile> bsaFile,
        std::shared_ptr<ModManagerDirectory::Mod> mod, const bool& generated = false);

    /**
     * @brief Convert a list of wstrings to a LPCWSTRs
     *
     * @param original original list of wstrings to convert
     * @return std::vector<LPCWSTR> list of LPCWSTRs
     */
    [[nodiscard]] static auto convertWStringToLPCWSTRVector(const std::vector<std::wstring>& original)
        -> std::vector<LPCWSTR>;

    /**
     * @brief Checks whether a glob matches a provided list of globs
     *
     * @param str String to check
     * @param winningGlob Last glob that one (for performance)
     * @param globList List of globs to check
     * @return true Any glob is the list mated
     * @return false No globs in the list matched
     */
    static auto checkGlob(const LPCWSTR& str, LPCWSTR& winningGlob, const std::vector<LPCWSTR>& globList) -> bool;

    static auto readINIValue(const std::filesystem::path& iniPath, const std::wstring& section, const std::wstring& key,
        const bool& logging, const bool& firstINIRead) -> std::wstring;
};
