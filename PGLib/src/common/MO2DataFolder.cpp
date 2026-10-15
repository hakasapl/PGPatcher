#include "common/MO2DataFolder.hpp"

#include "PGModManager.hpp"
#include "common/BethesdaDirectory.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace {

/**
 * @brief The volume GUID path of a folder: the folder's path with the mount point replaced by the volume name,
 * \\?\Volume{...}\. Empty when the volume cannot be determined.
 *
 * usvfs matches the paths of the calls it hooks against its tree by drive letter, so a volume GUID path reaches the
 * folder on disk: it is neither redirected nor merged with the files of the mods.
 */
std::filesystem::path volumeGUIDPath(const std::filesystem::path& folder)
{
    std::wstring mount(MAX_PATH, L'\0');
    if (!GetVolumePathNameW(folder.c_str(), mount.data(), MAX_PATH))
        return { };
    mount.resize(std::wcslen(mount.c_str()));

    std::wstring volume(MAX_PATH, L'\0');
    if (!GetVolumeNameForVolumeMountPointW(mount.c_str(), volume.data(), MAX_PATH))
        return { };
    volume.resize(std::wcslen(volume.c_str()));

    const auto folderStr = folder.wstring();
    if (!boost::istarts_with(folderStr, mount))
        return { };

    return volume + folderStr.substr(mount.size());
}

/// @brief Opens a directory for listing, INVALID_HANDLE_VALUE on failure.
HANDLE openDirectory(const std::filesystem::path& path)
{
    return CreateFileW(path.c_str(),
                       FILE_LIST_DIRECTORY,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       nullptr,
                       OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS,
                       nullptr);
}

/// @brief The identity of an open file on its volume, nothing on failure.
std::optional<FILE_ID_INFO> identityOfHandle(HANDLE handle)
{
    FILE_ID_INFO identity { };
    if (!GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
        return std::nullopt;

    return identity;
}

/// @brief Whether two directories are the same one on disk, whatever the paths that opened them (a junction, a short
/// name or a substituted drive spell the same folder differently).
bool isSameDirectory(const std::filesystem::path& first,
                     const std::filesystem::path& second)
{
    HANDLE firstHandle = openDirectory(first);
    if (firstHandle == INVALID_HANDLE_VALUE)
        return false;
    const std::unique_ptr<void, decltype(&CloseHandle)> firstCloser(firstHandle, &CloseHandle);

    HANDLE secondHandle = openDirectory(second);
    if (secondHandle == INVALID_HANDLE_VALUE)
        return false;
    const std::unique_ptr<void, decltype(&CloseHandle)> secondCloser(secondHandle, &CloseHandle);

    const auto firstIdentity = identityOfHandle(firstHandle);
    const auto secondIdentity = identityOfHandle(secondHandle);
    if (!firstIdentity || !secondIdentity)
        return false;

    return firstIdentity->VolumeSerialNumber == secondIdentity->VolumeSerialNumber
        && std::ranges::equal(firstIdentity->FileId.Identifier, secondIdentity->FileId.Identifier);
}

/**
 * @brief Lists a directory through a handle on its volume GUID path, what the directory holds on disk.
 *
 * @param volumePath Volume GUID path of the directory.
 * @param path The directory's path as the process sees it, for the links in it.
 * @return The entries, as FileUtil::listDirectory() reports them, or nothing when the directory cannot be opened.
 */
std::optional<std::vector<FileUtil::DirectoryEntry>> listDirectoryOnDisk(const std::filesystem::path& volumePath,
                                                                         const std::filesystem::path& path)
{
    HANDLE handle = openDirectory(volumePath);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;
    const std::unique_ptr<void, decltype(&CloseHandle)> handleCloser(handle, &CloseHandle);

    std::vector<FileUtil::DirectoryEntry> entries;
    std::vector<std::byte> buffer(static_cast<std::size_t>(64) * 1024);
    bool isFirst = true;
    while (true) {
        if (!GetFileInformationByHandleEx(handle,
                                          isFirst ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo,
                                          buffer.data(),
                                          static_cast<DWORD>(buffer.size()))) {
            if (GetLastError() == ERROR_NO_MORE_FILES)
                break;
            return std::nullopt;
        }
        isFirst = false;

        std::size_t offset = 0;
        while (true) {
            const auto* info = static_cast<const FILE_ID_BOTH_DIR_INFO*>(
                static_cast<const void*>(std::next(buffer.data(), static_cast<std::ptrdiff_t>(offset))));
            const std::wstring_view name(std::data(info->FileName), info->FileNameLength / sizeof(wchar_t));
            if (name != L"." && name != L"..") {
                FileUtil::DirectoryEntry entry;
                entry.name = name;
                entry.isHidden = (info->FileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0U;

                if (info->FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    // The same as FileUtil::listDirectory(): the entry is the link, type, size and write time are
                    // its target's, and only symbolic links and junctions count as links.
                    const auto tag = static_cast<DWORD>(info->EaSize);
                    entry.isLink = tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT;

                    const std::filesystem::path linkPath = path / entry.name;
                    std::error_code ec;
                    const auto status = std::filesystem::status(linkPath, ec);
                    entry.isDirectory = std::filesystem::is_directory(status);
                    entry.isRegularFile = std::filesystem::is_regular_file(status);

                    ec.clear();
                    entry.mtime = std::filesystem::last_write_time(linkPath, ec).time_since_epoch().count();

                    ec.clear();
                    const auto size = entry.isDirectory ? 0 : std::filesystem::file_size(linkPath, ec);
                    entry.size = ec ? 0 : size;
                } else {
                    entry.isDirectory = (info->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
                    entry.isRegularFile = !entry.isDirectory;
                    entry.mtime = info->LastWriteTime.QuadPart;
                    if (!entry.isDirectory)
                        entry.size = static_cast<uint64_t>(info->EndOfFile.QuadPart);
                }

                entries.push_back(std::move(entry));
            }

            if (!info->NextEntryOffset)
                break;
            offset += info->NextEntryOffset;
        }
    }

    return entries;
}

/// @brief A lister of the folder on disk, or nothing when the folder cannot be reached through its volume GUID path.
std::optional<std::function<std::optional<std::vector<FileUtil::DirectoryEntry>>(const std::filesystem::path&)>>
onDiskLister(const std::filesystem::path& folder)
{
    const auto volumePath = volumeGUIDPath(folder);
    if (volumePath.empty())
        return std::nullopt;

    // The volume path has to open the folder itself (the game folder may be reached through a junction or a short
    // name), which the identities of the two directories tell.
    if (!isSameDirectory(volumePath, folder))
        return std::nullopt;

    return [volumePath, folder](const std::filesystem::path& relDir) {
        return listDirectoryOnDisk(relDir.empty() ? volumePath : volumePath / relDir,
                                   relDir.empty() ? folder : folder / relDir);
    };
}

/// @brief A lister through the ordinary directory listing, what the process sees.
std::function<std::optional<std::vector<FileUtil::DirectoryEntry>>(const std::filesystem::path&)>
processViewLister(const std::filesystem::path& folder)
{
    return [folder](const std::filesystem::path& relDir) -> std::optional<std::vector<FileUtil::DirectoryEntry>> {
        try {
            return FileUtil::listDirectory(relDir.empty() ? folder : folder / relDir, true);
        } catch (const std::filesystem::filesystem_error&) {
            return std::nullopt;
        }
    };
}

}

MO2DataFolder MO2DataFolder::read(const std::filesystem::path& instanceDir,
                                  const std::filesystem::path& dataFolder,
                                  const Options& options)
{
    MO2DataFolder view;
    view.m_instanceDir = instanceDir;

    if (!PGModManager::isValidMO2InstanceDir(instanceDir)) {
        throw std::runtime_error("Mod Organizer 2 ini file does not exist: "
                                 + StringUtil::utf16toUTF8((instanceDir / L"modorganizer.ini").wstring()));
    }

    const auto [profilesDir, modsDir] = PGModManager::mo2FilePaths(instanceDir);
    view.m_modsDir = modsDir;
    view.m_profile = PGModManager::selectedProfileFromInstanceDir(instanceDir);
    const auto modNames = PGModManager::mo2EnabledMods(profilesDir / view.m_profile / L"modlist.txt");

    // The sources in the order they win: MO2 links the mods from the last line of modlist.txt to the first and the
    // overwrite folder after them, and a later link replaces an earlier one; the files on disk are below them all.
    std::error_code ec;
    const auto overwriteDir = PGModManager::mo2OverwriteDir(instanceDir);
    if (std::filesystem::is_directory(overwriteDir, ec))
        view.m_sources.push_back({ .kind = SourceKind::Overwrite, .name = { }, .folder = overwriteDir, .order = 0 });

    int order = 0;
    for (const auto& name : modNames) {
        const auto folder = modsDir / name;
        ec.clear();
        if (!std::filesystem::is_directory(folder, ec)) {
            Logger::warn(L"The folder of the mod {} does not exist, Mod Organizer 2 cannot serve it", name);
            continue;
        }

        view.m_sources.push_back({ .kind = SourceKind::Mod, .name = name, .folder = folder, .order = order++ });
    }
    // The real data folder: what it holds on disk, below the files usvfs merges into it. When it cannot be reached
    // through its volume, the listing is the one the process sees, with the files of the mods in it, which lose to
    // the mods' own entries, at the cost usvfs charges for every merged file.
    DirectoryLister dataFolderLister;
    if (options.shouldListDataFolder) {
        view.m_sources.push_back({ .kind = SourceKind::DataFolder, .name = { }, .folder = dataFolder, .order = 0 });
        auto onDisk = onDiskLister(dataFolder);
        view.m_isDataFolderListedOnDisk = onDisk.has_value();
        if (onDisk) {
            dataFolderLister = std::move(*onDisk);
        } else {
            Logger::info(L"The data folder {} cannot be listed on disk, it is listed as the process sees it",
                         dataFolder.wstring());
            dataFolderLister = processViewLister(dataFolder);
        }
    }

    const auto skipSuffixes = PGModManager::mo2SkipFileSuffixes(instanceDir);
    const auto skipFolders = PGModManager::mo2SkipDirectories(instanceDir);
    const auto isNeverSkipped = [](const FileUtil::DirectoryEntry&) { return false; };

    // Every source is listed on its own, the sources spread over one thread per hardware thread.
    std::vector<std::vector<File>> listings(view.m_sources.size());
    const auto listOne = [&](std::size_t index) {
        const auto& source = view.m_sources[index];
        try {
            listings[index] = source.kind == SourceKind::DataFolder
                ? walkFolder(source.folder, index, options, dataFolderLister, isNeverSkipped, isNeverSkipped)
                : listSource(source, index, options, skipSuffixes, skipFolders);
        } catch (const std::exception& e) {
            Logger::error(L"Unable to list {}: {}", source.folder.wstring(), StringUtil::utf8toUTF16(e.what()));
        }
    };

    if (options.shouldMultithread && view.m_sources.size() > 1) {
        std::atomic<std::size_t> nextSource { 0 };
        const auto hardwareThreads = std::max(1U, std::thread::hardware_concurrency());
        const auto threadCount = std::min<std::size_t>(view.m_sources.size(), hardwareThreads);
        std::vector<std::jthread> threads;
        threads.reserve(threadCount);
        for (std::size_t i = 0; i < threadCount; ++i) {
            threads.emplace_back([&listOne, &nextSource, &view] {
                for (auto index = nextSource.fetch_add(1); index < view.m_sources.size();
                     index = nextSource.fetch_add(1)) {
                    listOne(index);
                }
            });
        }
    } else {
        for (std::size_t index = 0; index < view.m_sources.size(); ++index)
            listOne(index);
    }

    // The first source that has a path wins it, and the first mod with a copy a walk of the mod folders would map
    // owns it (PGModManager maps every top level archive, and below the folders to map nothing hidden). The key is
    // the file map's key, see BethesdaDirectory.
    std::size_t total = 0;
    for (const auto& listing : listings)
        total += listing.size();
    std::unordered_map<std::wstring, std::size_t> indexByKey;
    indexByKey.reserve(total);
    view.m_files.reserve(total);
    for (std::size_t sourceIndex = 0; sourceIndex < listings.size(); ++sourceIndex) {
        const bool isMod = view.m_sources[sourceIndex].kind == SourceKind::Mod;
        for (auto& file : listings[sourceIndex]) {
            const bool isWalked = !file.isHidden && !file.isInHiddenFolder;
            const bool isTopLevel = file.relPath.find_first_of(L"\\/") == std::wstring::npos;
            const bool isOwnable = isMod && (isWalked || isTopLevel);

            const auto [it, isNew] = indexByKey.try_emplace(boost::to_lower_copy(file.relPath), view.m_files.size());
            if (isNew)
                view.m_files.push_back(std::move(file));

            auto& winner = view.m_files[it->second];
            if (isOwnable && winner.ownerSource == noSource)
                winner.ownerSource = sourceIndex;
        }
    }
    std::ranges::sort(view.m_files, { }, &File::relPath);
    view.m_files.shrink_to_fit();

    return view;
}

std::vector<MO2DataFolder::File> MO2DataFolder::listSource(const Source& source,
                                                           std::size_t sourceIndex,
                                                           const Options& options,
                                                           const std::vector<std::wstring>& skipSuffixes,
                                                           const std::vector<std::wstring>& skipFolders)
{
    // usvfs leaves the files and folders of MO2's skip settings out when it links a mod.
    const auto isSkippedFile = [&skipSuffixes](const std::wstring& name) {
        return std::ranges::any_of(skipSuffixes,
                                   [&name](const std::wstring& suffix) { return boost::iends_with(name, suffix); });
    };
    const auto isSkippedFolder = [&skipFolders](const std::wstring& name) {
        return std::ranges::any_of(skipFolders,
                                   [&name](const std::wstring& folder) { return boost::iequals(name, folder); });
    };

    return walkFolder(
        source.folder,
        sourceIndex,
        options,
        processViewLister(source.folder),
        [&isSkippedFile](const FileUtil::DirectoryEntry& entry) { return isSkippedFile(entry.name); },
        [&isSkippedFolder](const FileUtil::DirectoryEntry& entry) { return isSkippedFolder(entry.name); });
}

std::vector<MO2DataFolder::File> MO2DataFolder::walkFolder(const std::filesystem::path& folder,
                                                           std::size_t sourceIndex,
                                                           const Options& options,
                                                           const DirectoryLister& list,
                                                           const EntryPredicate& isSkippedFile,
                                                           const EntryPredicate& isSkippedFolder)
{
    std::vector<File> files;
    const auto addFile = [&files, &isSkippedFile, sourceIndex](
                             std::wstring relPath, const FileUtil::DirectoryEntry& entry, bool isInHiddenFolder) {
        if (isSkippedFile(entry))
            return;

        File file;
        file.relPath = std::move(relPath);
        file.mtime = entry.mtime;
        file.size = entry.size;
        file.isHidden = BethesdaDirectory::isHidden(entry);
        file.isInHiddenFolder = isInHiddenFolder;
        file.source = sourceIndex;
        files.push_back(std::move(file));
    };

    // The top level is listed without recursion, like BethesdaDirectory lists the data folder's.
    const auto topLevel = list({ });
    if (!topLevel) {
        throw std::filesystem::filesystem_error(
            "Unable to list directory", folder, std::make_error_code(std::errc::no_such_file_or_directory));
    }
    for (const auto& entry : *topLevel)
        if (!entry.isDirectory)
            addFile(entry.name, entry, false);

    for (const auto& mappedFolder : options.foldersToMap) {
        std::error_code ec;
        if (!std::filesystem::is_directory(folder / mappedFolder, ec))
            continue;

        FileUtil::DirectoryEntry folderEntry;
        folderEntry.name = mappedFolder.wstring();
        folderEntry.isDirectory = true;
        if (isSkippedFolder(folderEntry))
            continue;

        // The walk of FileUtil::walkDirectory(): links are visited, never followed. A hidden folder is walked, as the
        // game reads it, and what is below it is flagged.
        const std::wstring prefix = (mappedFolder / L"").wstring();
        std::vector<std::wstring> hiddenPrefixes;
        std::vector<std::pair<std::filesystem::path, std::wstring>> pendingFolders;
        pendingFolders.emplace_back(mappedFolder, std::wstring());
        while (!pendingFolders.empty()) {
            const auto [relDir, relPrefix] = std::move(pendingFolders.back());
            pendingFolders.pop_back();

            const auto entries = list(relDir);
            if (!entries)
                continue;

            for (const auto& entry : *entries) {
                const std::wstring relPath = relPrefix + entry.name;
                const bool isInHiddenFolder = std::ranges::any_of(
                    hiddenPrefixes, [&relPath](const std::wstring& hidden) { return relPath.starts_with(hidden); });
                if (entry.isDirectory) {
                    if (isSkippedFolder(entry))
                        continue;
                    if (!isInHiddenFolder && BethesdaDirectory::isHidden(entry))
                        hiddenPrefixes.push_back(relPath + std::filesystem::path::preferred_separator);
                    if (!entry.isLink)
                        pendingFolders.emplace_back(relDir / entry.name,
                                                    relPath + std::filesystem::path::preferred_separator);
                    continue;
                }

                addFile(prefix + relPath, entry, isInHiddenFolder);
            }
        }
    }

    return files;
}

void MO2DataFolder::forEachLooseFile(const std::function<void(const BethesdaDirectory::LooseFile&)>& visitor) const
{
    for (const auto& file : m_files)
        visitor(file);
}
