#pragma once

#include "common/BethesdaDirectory.hpp"
#include "util/FileUtil.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

/**
 * @brief The data folder as Mod Organizer 2 composes it for a profile, built from the instance's own files.
 *
 * MO2 links every enabled mod folder into the game's data folder with its virtual file system (usvfs) and the
 * overwrite folder on top of them. usvfs merges every listing of the virtual folder file by file, at tens of
 * microseconds per file, which makes the file map of a large profile take half a minute. The same files follow from
 * the instance: the mods of the profile's modlist.txt, of which the first line wins, the overwrite folder, which
 * wins over every mod, and the files of the real data folder below everything. The mod folders are listed on disk,
 * in parallel, and the real data folder through a handle usvfs does not reroute.
 *
 * The view is the one usvfs serves when it comes from the instance and profile that launched the process
 * (PGModManager::runningMO2InstanceDir()). Files MO2 skips (the skip suffixes and skip folders of its settings) are
 * left out as usvfs leaves them out; hidden files stay in, as the game sees them, and are flagged for the readers
 * that skip them.
 */
class MO2DataFolder : public BethesdaDirectory::LooseFileSource {
public:
    /// @brief Where a file of the view comes from.
    enum class SourceKind : uint8_t { Mod, Overwrite, DataFolder };

    /**
     * @struct Source
     * @brief A folder of the view: an enabled mod, the overwrite folder or the real data folder.
     */
    struct Source {
        SourceKind kind = SourceKind::Mod;
        std::wstring name; /**< The mod's name as modlist.txt lists it, empty for the other kinds */
        std::filesystem::path folder; /**< The folder on disk */
        int order = 0; /**< The mod's index among the enabled mods of modlist.txt, 0 for the first line, which wins */
    };

    /// @brief The source index of a file no mod owns.
    static constexpr std::size_t noSource = static_cast<std::size_t>(-1);

    /**
     * @struct File
     * @brief A file of the view: the one the game sees at its path.
     */
    struct File : BethesdaDirectory::LooseFile {
        std::size_t source = 0; /**< Index into sources() of the file the game sees */
        /// @brief Index into sources() of the mod that owns the file for the mod attribution: the first mod of
        /// modlist.txt with a copy that a walk of the mod folders maps (not hidden below the folders to map), as
        /// PGModManager walks them, whichever source the game sees. noSource when no mod has one.
        std::size_t ownerSource = noSource;
    };

    /**
     * @struct Options
     * @brief What is read of the instance.
     */
    struct Options {
        /// @brief Lower case top level folders of the data folder that are walked. The top level itself is always
        /// listed, without recursion, as BethesdaDirectory lists it.
        std::unordered_set<std::filesystem::path> foldersToMap;
        bool shouldMultithread = true;
        /// @brief Whether the real data folder is listed below the mods. Off for a reader that only attributes mods.
        bool shouldListDataFolder = true;
    };

    /**
     * @brief Reads the instance's modorganizer.ini and the selected profile's modlist.txt and lists the enabled mods,
     * the overwrite folder and the real data folder.
     *
     * A mod folder that does not exist is left out with a warning, as MO2 cannot serve it either.
     *
     * @param instanceDir Folder of the MO2 instance (the one with modorganizer.ini).
     * @param dataFolder The game's data folder, the folder MO2 links the mods into.
     * @param options What is read.
     * @throws std::runtime_error if modorganizer.ini or modlist.txt does not exist.
     */
    [[nodiscard]] static MO2DataFolder read(const std::filesystem::path& instanceDir,
                                            const std::filesystem::path& dataFolder,
                                            const Options& options);

    /// @brief The folders of the view in the order they win: the overwrite folder, the mods as modlist.txt lists
    /// them, the real data folder.
    [[nodiscard]] const std::vector<Source>& sources() const { return m_sources; }

    /// @brief One entry per path, the file the game sees there, sorted by path.
    [[nodiscard]] const std::vector<File>& files() const { return m_files; }

    [[nodiscard]] const std::filesystem::path& instanceDir() const { return m_instanceDir; }
    [[nodiscard]] const std::filesystem::path& modsDir() const { return m_modsDir; }
    [[nodiscard]] const std::wstring& profile() const { return m_profile; }

    /// @brief Whether the real data folder was listed on disk, through its volume, rather than as the process sees
    /// it. False as well when it was not listed at all.
    [[nodiscard]] bool isDataFolderListedOnDisk() const { return m_isDataFolderListedOnDisk; }

    /// @brief Every file of the view, as a LooseFileSource for BethesdaDirectory.
    void forEachLooseFile(const std::function<void(const BethesdaDirectory::LooseFile&)>& visitor) const override;

private:
    /// @brief Lists one directory of a folder, by its path relative to the folder, or nothing when it cannot be
    /// listed.
    using DirectoryLister
        = std::function<std::optional<std::vector<FileUtil::DirectoryEntry>>(const std::filesystem::path&)>;
    using EntryPredicate = std::function<bool(const FileUtil::DirectoryEntry&)>;

    MO2DataFolder() = default;

    /**
     * @brief Lists a mod or the overwrite folder as the process sees it, leaving out what MO2 does not link.
     *
     * @param source The folder.
     * @param sourceIndex Its index in sources().
     * @param options The folders to map.
     * @param skipSuffixes File name suffixes MO2 does not link, compared without regard to case.
     * @param skipFolders Folder names MO2 does not link, compared without regard to case.
     * @return The files found, in listing order.
     */
    static std::vector<File> listSource(const Source& source,
                                        std::size_t sourceIndex,
                                        const Options& options,
                                        const std::vector<std::wstring>& skipSuffixes,
                                        const std::vector<std::wstring>& skipFolders);

    /**
     * @brief Lists a folder the way BethesdaDirectory lists the data folder: its top level without recursion, the
     * folders to map with their subfolders. Links are visited and never followed; a hidden folder is walked and
     * what is below it flagged.
     *
     * @param folder The folder.
     * @param sourceIndex Its index in sources().
     * @param options The folders to map.
     * @param list Lists one directory of the folder.
     * @param isSkippedFile Whether a file is left out.
     * @param isSkippedFolder Whether a folder is left out with everything below it.
     * @throws std::filesystem::filesystem_error if the folder itself cannot be listed.
     */
    static std::vector<File> walkFolder(const std::filesystem::path& folder,
                                        std::size_t sourceIndex,
                                        const Options& options,
                                        const DirectoryLister& list,
                                        const EntryPredicate& isSkippedFile,
                                        const EntryPredicate& isSkippedFolder);

    std::filesystem::path m_instanceDir;
    std::filesystem::path m_modsDir;
    std::wstring m_profile;
    std::vector<Source> m_sources;
    std::vector<File> m_files;
    bool m_isDataFolderListedOnDisk = false;
};
