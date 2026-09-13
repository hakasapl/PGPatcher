#include "common/BethesdaDirectory.hpp"

#include "common/BethesdaGame.hpp"
#include "util/ContainerUtil.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <binary_io/any_stream.hpp>
#include <binary_io/memory_stream.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/crc.hpp>
#include <bsa/tes4.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fileapi.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <minwindef.h>
#include <mutex>
#include <shared_mutex>
#include <shlwapi.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>
#include <winnt.h>

using namespace StringUtil;

BethesdaDirectory::BethesdaDirectory(BethesdaGame* bg,
                                     std::unordered_set<std::filesystem::path> foldersToMap,
                                     std::filesystem::path generatedPath)
    : m_generatedDir(std::move(generatedPath))
    , m_bg(bg)
    , m_foldersToMap(std::move(foldersToMap))
{
    // Assign instance vars.
    m_dataDir = std::filesystem::path(this->m_bg->getGameDataPath());

    // Log starting message.
    Logger::info(L"Opening Data Folder \"{}\"", m_dataDir.wstring());
}

BethesdaDirectory::BethesdaDirectory(std::filesystem::path dataPath,
                                     std::unordered_set<std::filesystem::path> foldersToMap,
                                     std::filesystem::path generatedPath)
    : m_dataDir(std::move(dataPath))
    , m_generatedDir(std::move(generatedPath))
    , m_foldersToMap(std::move(foldersToMap))
    , m_bg(nullptr)
{
    // Log starting message.
    Logger::info(L"Opening Data Folder \"{}\"", m_dataDir.wstring());
}

//
// Constant Definitions.
//
auto BethesdaDirectory::getINIBSAFields() -> std::vector<std::string>
{
    // These fields will be searched in ini files for manually specified BSA.
    // Loading.
    const static std::vector<std::string> iniBSAFields
        = { "sResourceArchiveList", "sResourceArchiveList2", "sResourceArchiveListBeta" };

    return iniBSAFields;
}

auto BethesdaDirectory::getExtensionBlocklist() -> std::vector<std::wstring>
{
    // Any file that ends with these strings will be ignored.
    // Allowed BSAs etc. to be hidden from the file map since this object is an.
    // Abstraction of the data directory that no longer factors BSAs for.
    // Downstream users.
    const static std::vector<std::wstring> extensionBlocklist = { L".bsa", L".esp", L".esl", L".esm" };

    return extensionBlocklist;
}

auto BethesdaDirectory::checkGlob(const std::wstring& str,
                                  const std::vector<std::wstring>& globList) -> bool
{
    // Convert wstring vector to LPCWSTR vector.
    std::vector<LPCWSTR> globListCstr = convertWStringToLPCWSTRVector(globList);

    // Convert wstring to LPCWSTR.
    LPCWSTR strCstr = str.c_str();

    // Check if string matches any glob.
    return std::ranges::any_of(globListCstr, [&](LPCWSTR glob) { return PathMatchSpecW(strCstr, glob); });
}

void BethesdaDirectory::populateFileMap(bool includeBSAs)
{
    // Clear map before populating.
    {
        const std::unique_lock lock(m_fileMapMutex);
        m_fileMap.clear();
        m_generatedFileRestoreMap.clear();
    }

    if (includeBSAs && m_bg != nullptr) {
        // Add BSA files to file map.
        addBSAFilesToMap();
    }

    // Add loose files to file map.
    addLooseFilesToMap();
}

auto BethesdaDirectory::getFileMap() const -> const std::map<std::filesystem::path,
                                                             BethesdaDirectory::BethesdaFile>&
{
    return m_fileMap;
}

auto BethesdaDirectory::getFile(const std::filesystem::path& relPath) -> std::vector<std::byte>
{
    // Find bsa/loose file to open.
    const BethesdaFile& file = getFileFromMap(relPath);
    if (file.path.empty())
        throw std::runtime_error("File not found in file map");

    if (s_threadQueryObserver != nullptr)
        s_threadQueryObserver->onGetFile(relPath, buildIdentity(file));

    std::vector<std::byte> outFileBytes;
    const std::shared_ptr<BSAFile> bsaStruct = file.bsaFile;
    if (bsaStruct == nullptr) {
        std::filesystem::path filePath;
        if (file.generated)
            filePath = m_generatedDir / relPath;
        else
            filePath = m_dataDir / relPath;

        outFileBytes = FileUtil::getFileBytes(filePath);
    } else {
        const std::filesystem::path bsaPath = bsaStruct->path;

        // This is a bsa archive file.
        const bsa::tes4::version& bsaVersion = bsaStruct->version;
        const bsa::tes4::archive& bsaObj = bsaStruct->archive;

        std::string parentPath = utf16toASCII(relPath.parent_path().wstring());
        std::string filename = utf16toASCII(relPath.filename().wstring());

        const auto& file = bsaObj[parentPath][filename];
        if (file) {
            binary_io::any_ostream aos { std::in_place_type<binary_io::memory_ostream> };
            // Read file from output stream.
            try {
                file->write(aos, bsaVersion);
            } catch (...) {
                Logger::error(L"Failed to read file: {}", relPath.wstring());
                return { };
            }

            auto& s = aos.get<binary_io::memory_ostream>();
            outFileBytes = s.rdbuf();
        } else {
            throw std::runtime_error("File not found in BSA archive");
        }
    }

    if (outFileBytes.empty())
        return { };

    return outFileBytes;
}

void BethesdaDirectory::addGeneratedFile(const std::filesystem::path& relPath)
{
    const std::unique_lock lock(m_fileMapMutex);

    const auto existingIt = m_fileMap.find(relPath);
    if (existingIt != m_fileMap.end() && !existingIt->second.generated) {
        // Keep the original/native source so deletion of generated entries can restore it.
        m_generatedFileRestoreMap[relPath] = existingIt->second;
    }

    const BethesdaFile generatedFile
        = { .path = relPath, .bsaFile = nullptr, .generated = true, .mtime = 0, .size = 0 };
    m_fileMap[relPath] = generatedFile;
}

void BethesdaDirectory::clearGeneratedFiles()
{
    const std::unique_lock lock(m_fileMapMutex);

    for (auto it = m_fileMap.begin(); it != m_fileMap.end();) {
        if (it->second.generated) {
            const auto restoreIt = m_generatedFileRestoreMap.find(it->first);
            if (restoreIt != m_generatedFileRestoreMap.end()) {
                it->second = restoreIt->second;
                m_generatedFileRestoreMap.erase(restoreIt);
                ++it;
            } else {
                it = m_fileMap.erase(it);
            }
        } else {
            ++it;
        }
    }
}

auto BethesdaDirectory::isLooseFile(const std::filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");
    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.bsaFile == nullptr;
}

auto BethesdaDirectory::isBSAFile(const std::filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.bsaFile != nullptr;
}

auto BethesdaDirectory::isFile(const std::filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = getFileFromMap(relPath);
    const bool exists = !file.path.empty();

    if (s_threadQueryObserver != nullptr)
        s_threadQueryObserver->onIsFile(relPath, exists, exists && file.generated);

    return exists;
}

auto BethesdaDirectory::buildIdentity(const BethesdaFile& file) -> FileIdentity
{
    FileIdentity identity;

    if (file.path.empty()) {
        identity.kind = FileIdentity::Kind::None;
        return identity;
    }

    if (file.generated) {
        identity.kind = FileIdentity::Kind::GENERATED;
        return identity;
    }

    if (file.bsaFile != nullptr) {
        identity.kind = FileIdentity::Kind::BSA;
        identity.bsaRelPath = file.bsaFile->relPath.wstring();
        identity.bsaMtime = file.bsaFile->mtime;
        identity.bsaSize = file.bsaFile->size;
        return identity;
    }

    identity.kind = FileIdentity::Kind::LOOSE;
    identity.mtime = file.mtime;
    identity.size = file.size;
    return identity;
}

auto BethesdaDirectory::getFileIdentity(const std::filesystem::path& relPath) -> FileIdentity
{
    const BethesdaFile file = getFileFromMap(relPath);
    return buildIdentity(file);
}

void BethesdaDirectory::setThreadFileQueryObserver(FileQueryObserver* observer) { s_threadQueryObserver = observer; }

auto BethesdaDirectory::isGenerated(const std::filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.generated;
}

auto BethesdaDirectory::getLooseFileFullPath(const std::filesystem::path& relPath) -> std::filesystem::path
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = getFileFromMap(relPath);

    if (file.generated)
        return m_generatedDir / relPath;

    return m_dataDir / relPath;
}

auto BethesdaDirectory::getDataPath() const -> std::filesystem::path { return m_dataDir; }

auto BethesdaDirectory::getGeneratedPath() const -> std::filesystem::path { return m_generatedDir; }

void BethesdaDirectory::addBSAFilesToMap()
{
    if (m_bg == nullptr)
        throw std::runtime_error("BethesdaGame object is not set which is required to load BSA files");

    Logger::info("Adding BSA files to file map.");

    // Get list of BSA files.
    const std::vector<std::wstring> bsaFiles = getBSALoadOrder();

    // Loop through each BSA file.
    for (const auto& bsaName : bsaFiles) {
        // Add bsa to file map.
        addBSAToFileMap(bsaName);
    }
}

void BethesdaDirectory::addLooseFilesToMap()
{
    Logger::info("Adding loose files to file map.");

    // Map top level folder (not recursive).
    for (auto it
         = std::filesystem::directory_iterator(m_dataDir, std::filesystem::directory_options::skip_permission_denied);
         it != std::filesystem::directory_iterator();
         ++it) {
        const auto& entry = *it;

        if (isHidden(entry.path()) || entry.is_directory())
            continue;

        const std::filesystem::path& filePath = entry.path();
        std::filesystem::path relativePath = filePath.lexically_relative(m_dataDir);
        relativePath = boost::to_lower_copy(relativePath.wstring());

        // Check type of file, skip BSAs and ESPs.
        if (!isFileAllowed(filePath))
            continue;

        // Directory_entry caches size and write time from the directory listing so these are free.
        std::error_code ec;
        const auto mtime = entry.last_write_time(ec).time_since_epoch().count();
        ec.clear();
        const auto size = entry.file_size(ec);

        updateFileMap(relativePath, nullptr, false, static_cast<int64_t>(mtime), ec ? 0 : size);
    }

    // Loop through each folder to map.
    for (const auto& folder : m_foldersToMap) {
        // Check if folder exists.
        const auto curCheckFolder = m_dataDir / folder;
        if (!std::filesystem::exists(curCheckFolder))
            continue;

        for (auto it = std::filesystem::recursive_directory_iterator(
                 curCheckFolder, std::filesystem::directory_options::skip_permission_denied);
             it != std::filesystem::recursive_directory_iterator();
             ++it) {
            const auto entry = *it;

            if (isHidden(entry.path())) {
                if (entry.is_directory()) {
                    // If it's a directory, don't recurse into it.
                    it.disable_recursion_pending();
                }
                continue;
            }

            const std::filesystem::path& filePath = entry.path();
            std::filesystem::path relativePath = filePath.lexically_relative(m_dataDir);
            relativePath = boost::to_lower_copy(relativePath.wstring());

            // Check type of file, skip BSAs and ESPs.
            if (!isFileAllowed(filePath))
                continue;

            // Directory_entry caches size and write time from the directory listing so these are free.
            std::error_code ec;
            const auto mtime = entry.last_write_time(ec).time_since_epoch().count();
            ec.clear();
            const auto size = entry.is_directory() ? 0 : entry.file_size(ec);

            updateFileMap(relativePath, nullptr, false, static_cast<int64_t>(mtime), ec ? 0 : size);
        }
    }
}

void BethesdaDirectory::addBSAToFileMap(const std::wstring& bsaName)
{
    // Log message.
    Logger::debug(L"Adding files from {} to file map.", bsaName);

    bsa::tes4::archive bsaObj;
    const std::filesystem::path bsaPath = m_dataDir / bsaName;

    // Skip BSA if it doesn't exist (can happen if it's in the ini but not in the.
    // Data folder).
    if (!std::filesystem::exists(bsaPath)) {
        Logger::warn(L"BSA is in INI but does not exist: {}", bsaPath.wstring());
        return;
    }

    bsa::tes4::version bsaVersion = bsa::tes4::version::tes5;
    try {
        bsaVersion = bsaObj.read(bsaPath);
    } catch (...) {
        Logger::error(L"Failed to read BSA file version: {}", bsaName);
        return;
    }

    // Archive identity (used to detect changed archives without reading their contents).
    std::error_code ec;
    const auto bsaMtime
        = static_cast<int64_t>(std::filesystem::last_write_time(bsaPath, ec).time_since_epoch().count());
    ec.clear();
    const auto bsaSizeRaw = std::filesystem::file_size(bsaPath, ec);
    const uint64_t bsaSize = ec ? 0 : bsaSizeRaw;

    const std::shared_ptr<BSAFile> bsaStructPtr
        = std::make_shared<BSAFile>(bsaPath, boost::to_lower_copy(bsaName), bsaVersion, bsaObj, bsaMtime, bsaSize);

    // Loop iterator.
    for (const auto& fileEntry : bsaObj) {
        // Get file entry from pointer.
        try {
            // .second stores the files in the folder.
            const auto fileName = fileEntry.second;

            // Loop through files in folder.
            for (const auto& entry : fileName) {

                if (!containsOnlyAscii(std::string(fileEntry.first.name()))
                    || !containsOnlyAscii(std::string(entry.first.name()))) {
                    Logger::warn(L"File {}\\{} in BSA {} contains non-ascii characters",
                                 windows1252toUTF16(std::string(fileEntry.first.name())),
                                 windows1252toUTF16(std::string(entry.first.name())),
                                 bsaName);

                    continue;
                }

                // Get folder name within the BSA vfs.
                const std::filesystem::path folderName = asciitoUTF16(std::string(fileEntry.first.name()));

                if (!m_foldersToMap.contains(folderName.begin()->wstring())) {
                    // Skip if folder is not in the list of folders to map.
                    continue;
                }

                // Get name of file.
                const std::wstring curEntry = asciitoUTF16(std::string(entry.first.name()));
                std::filesystem::path curPath = folderName / curEntry;
                curPath = boost::to_lower_copy(curPath.wstring());

                // Chekc if we should ignore this file.
                if (!isFileAllowed(curPath))
                    continue;

                // Add to filemap.
                updateFileMap(curPath, bsaStructPtr);
            }
        } catch (...) {
            Logger::error(L"Failed to get file pointer from BSA: {}", bsaName);
            continue;
        }
    }
}

auto BethesdaDirectory::getBSALoadOrder() const -> std::vector<std::wstring>
{
    // Get bsa files not loaded from esp (also initializes output vector).
    std::vector<std::wstring> outBSAOrder = getBSAFilesFromINIs();

    // Get esp priority list.
    const std::vector<std::wstring> loadOrder = m_bg->getActivePlugins(true);

    // List BSA files in data directory.
    const std::vector<std::wstring> allBSAFiles = getBSAFilesInDirectory();

    // Loop through each esp in the priority list.
    for (const auto& plugin : loadOrder) {
        // Add any BSAs to list.
        const std::vector<std::wstring> curFoundBSAs = findBSAFilesFromPluginName(allBSAFiles, plugin);
        ContainerUtil::concatenateVectorsWithoutDuplicates(outBSAOrder, curFoundBSAs);
    }

    // Log output.
    for (const auto& bsa : allBSAFiles)
        if (!ContainerUtil::isInVector(outBSAOrder, bsa))
            Logger::warn(L"BSA file {} not loaded by any active plugin or INI.", bsa);

    return outBSAOrder;
}

auto BethesdaDirectory::getModLookupFile(const std::filesystem::path& relPath) -> std::filesystem::path
{
    // Get file.
    const BethesdaFile& file = getFileFromMap(relPath);
    if (file.bsaFile != nullptr)
        return file.bsaFile->relPath;

    return relPath;
}

auto BethesdaDirectory::getBSAFilesFromINIs() const -> std::vector<std::wstring>
{
    // Output vector.
    std::vector<std::wstring> bsaFiles;

    // Find ini paths.
    const BethesdaGame::ININame iniLocs = m_bg->getINIPaths();

    std::vector<std::filesystem::path> iniFileOrder = { iniLocs.ini, iniLocs.iniCustom };

    // Find INIs in data folder.
    for (const auto& entry :
         std::filesystem::directory_iterator(m_dataDir, std::filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && toLowerASCII(entry.path().extension().wstring()) == L".ini")
            iniFileOrder.push_back(entry.path());
    }

    // Loop through each field.
    for (const auto& field : getINIBSAFields()) {
        // Loop through each ini file.
        std::wstring iniVal;
        for (const auto& iniPath : iniFileOrder) {
            const std::wstring curVal = readINIValue(iniPath, L"Archive", asciitoUTF16(field));
            if (curVal.empty())
                continue;

            iniVal = curVal;
        }

        if (iniVal.empty())
            continue;

        // Split into components.
        std::vector<std::wstring> iniComponents;
        boost::split(iniComponents, iniVal, boost::is_any_of(","));
        for (auto& bsa : iniComponents) {
            // Remove leading/trailing whitespace.
            boost::trim(bsa);

            // Add to output.
            ContainerUtil::addUniqueElement(bsaFiles, bsa);
        }
    }

    return bsaFiles;
}

auto BethesdaDirectory::getBSAFilesInDirectory() const -> std::vector<std::wstring>
{
    std::vector<std::wstring> bsaFiles;

    for (const auto& entry : std::filesystem::directory_iterator(this->m_dataDir)) {
        if (entry.is_regular_file()) {
            const auto fileExtension = entry.path().extension().wstring();
            // Only interested in BSA files.
            if (!boost::iequals(fileExtension, ".bsa"))
                continue;

            // Add to output.
            bsaFiles.push_back(entry.path().filename().wstring());
        }
    }

    return bsaFiles;
}

auto BethesdaDirectory::findBSAFilesFromPluginName(const std::vector<std::wstring>& bsaFileList,
                                                   const std::wstring& pluginPrefix) -> std::vector<std::wstring>
{
    std::vector<std::wstring> bsaFilesFound;
    const std::wstring pluginPrefixLower = boost::to_lower_copy(pluginPrefix);

    for (const std::wstring& bsa : bsaFileList) {
        const std::wstring bsaLower = boost::to_lower_copy(bsa);
        if (bsaLower.starts_with(pluginPrefixLower)) {
            if (bsaLower == pluginPrefixLower + L".bsa") {
                // Load bsa with the plugin name before any others.
                bsaFilesFound.insert(bsaFilesFound.begin(), bsa);
                continue;
            }

            // Skip any BSAs that may start with the prefix but belong to a different.
            // Plugin.
            std::wstring afterPrefix = bsa.substr(pluginPrefix.length());

            // Todo: Is this actually how the game handles BSA files? Example:
            // 3DNPC0.bsa, 3DNPC1.bsa, 3DNPC2.bsa are loaded, todo: but 3DNPC -
            // textures.bsa is also loaded, whats the logic there?
            if (afterPrefix.starts_with(L' ') && !afterPrefix.starts_with(L" -"))
                continue;

            if (!afterPrefix.starts_with(L' ') && (isdigit(afterPrefix[0]) == 0))
                continue;

            bsaFilesFound.push_back(bsa);
        }
    }

    return bsaFilesFound;
}

auto BethesdaDirectory::isFileAllowed(const std::filesystem::path& filePath) -> bool
{
    std::wstring fileExtension = filePath.extension().wstring();
    boost::algorithm::to_lower(fileExtension);

    return !ContainerUtil::isInVector(getExtensionBlocklist(), fileExtension);
}

// Helpers.

auto BethesdaDirectory::isPathAscii(const std::filesystem::path& path) -> bool
{
    return std::ranges::all_of(path.wstring(), [](wchar_t wc) { return wc <= asciiUpperBound; });
}

auto BethesdaDirectory::getFileFromMap(const std::filesystem::path& filePath) -> BethesdaDirectory::BethesdaFile
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const std::shared_lock lock(m_fileMapMutex);
    if (!m_fileMap.contains(filePath)) {
        return BethesdaFile {
            .path = std::filesystem::path(),
            .bsaFile = nullptr,
            .generated = false,
            .mtime = 0,
            .size = 0,
        };
    }

    return m_fileMap.at(filePath);
}

void BethesdaDirectory::updateFileMap(const std::filesystem::path& filePath,
                                      std::shared_ptr<BethesdaDirectory::BSAFile> bsaFile,
                                      const bool& generated,
                                      const int64_t& mtime,
                                      const uint64_t& size)
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const std::unique_lock lock(m_fileMapMutex);

    const BethesdaFile newBFile
        = { .path = filePath, .bsaFile = std::move(bsaFile), .generated = generated, .mtime = mtime, .size = size };

    m_fileMap[filePath] = newBFile;
}

auto BethesdaDirectory::isFileInBSA(const std::filesystem::path& file,
                                    const std::vector<std::wstring>& bsaFiles) -> bool
{
    if (isBSAFile(file)) {
        BethesdaFile const bethFile = getFileFromMap(file);
        std::filesystem::path const bsaFilepath = bethFile.bsaFile->path.filename();
        const std::wstring bsaFilename = bsaFilepath.wstring();

        if (std::ranges::any_of(
                bsaFiles, [&bsaFilename](std::wstring const& file) { return boost::iequals(file, bsaFilename); })) {
            return true;
        }
    }
    return false;
}

auto BethesdaDirectory::convertWStringToLPCWSTRVector(const std::vector<std::wstring>& original) -> std::vector<LPCWSTR>
{
    std::vector<LPCWSTR> output(original.size());
    for (size_t i = 0; i < original.size(); i++)
        output[i] = original[i].c_str();

    return output;
}

auto BethesdaDirectory::checkGlob(const LPCWSTR& str,
                                  LPCWSTR& winningGlob,
                                  const std::vector<LPCWSTR>& globList) -> bool
{
    if (!boost::equals(winningGlob, L"") && (PathMatchSpecW(str, winningGlob) != 0)) {
        // No winning glob, check all globs.
        return true;
    }

    for (const auto& glob : globList) {
        if (glob != winningGlob && (PathMatchSpecW(str, glob) != 0)) {
            winningGlob = glob;
            return true;
        }
    }

    return false;
}

auto BethesdaDirectory::isHidden(const std::filesystem::path& path) -> bool
{
    // Check if file is hidden in filesystem.
    DWORD const fileAttributes = GetFileAttributesW(path.c_str());
    if (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0)
        return true;

    // Check if file is a dotfile.
    if (path.filename().wstring().starts_with(L'.'))
        return true;

    // Check if file ends in .mohidden (MO2 hidden file).
    if (boost::iequals(path.extension().wstring(), ".mohidden"))
        return true;

    return false;
}

auto BethesdaDirectory::readINIValue(const std::filesystem::path& iniPath,
                                     const std::wstring& section,
                                     const std::wstring& key) -> std::wstring
{
    if (!std::filesystem::exists(iniPath))
        return L"";

    std::ifstream f(iniPath);
    if (!f.is_open())
        return L"";

    std::string curLine;
    std::string curSection;
    bool foundSection = false;

    while (getline(f, curLine)) {
        boost::trim(curLine);

        // Ignore comments.
        if (curLine.empty() || curLine[0] == ';' || curLine[0] == '#')
            continue;

        // Check if it's a section.
        if (curLine.front() == '[' && curLine.back() == ']') {
            curSection = curLine.substr(1, curLine.size() - 2);
            continue;
        }

        // Check if it's the correct section.
        if (boost::iequals(curSection, section))
            foundSection = true;

        if (!boost::iequals(curSection, section)) {
            // Exit if already checked section.
            if (foundSection)
                break;
            continue;
        }

        // Check key.
        const size_t pos = curLine.find('=');
        if (pos != std::string::npos) {
            // Found key value pair.
            std::string curKey = curLine.substr(0, pos);
            boost::trim(curKey);
            if (boost::iequals(curKey, key)) {
                std::string curValue = curLine.substr(pos + 1);
                boost::trim(curValue);
                return StringUtil::utf8toUTF16(curValue);
            }
        }
    }

    return L"";
}
