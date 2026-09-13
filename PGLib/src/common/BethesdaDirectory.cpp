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

BethesdaDirectory::BethesdaDirectory(BethesdaGame* bg,
                                     std::unordered_set<std::filesystem::path> foldersToMap,
                                     std::filesystem::path generatedPath)
    : m_dataDir(bg->gameDataPath())
    , m_generatedDir(std::move(generatedPath))
    , m_foldersToMap(std::move(foldersToMap))
    , m_bg(bg)
{
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
std::vector<std::string> BethesdaDirectory::inibsaFields()
{
    // These fields will be searched in ini files for manually specified BSA.
    // Loading.
    const static std::vector<std::string> iniBSAFields
        = { "sResourceArchiveList", "sResourceArchiveList2", "sResourceArchiveListBeta" };

    return iniBSAFields;
}

std::vector<std::wstring> BethesdaDirectory::extensionBlocklist()
{
    // Any file that ends with these strings will be ignored.
    // Allowed BSAs etc. to be hidden from the file map since this object is an
    // abstraction of the data directory that no longer factors BSAs for
    // downstream users.
    const static std::vector<std::wstring> extensionBlocklist = { L".bsa", L".esp", L".esl", L".esm" };

    return extensionBlocklist;
}

bool BethesdaDirectory::checkGlob(const std::wstring& str,
                                  const std::vector<std::wstring>& globList)
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

    if (includeBSAs && m_bg) {
        // Add BSA files to file map.
        addBSAFilesToMap();
    }

    // Add loose files to file map.
    addLooseFilesToMap();
}

auto BethesdaDirectory::fileMap() const -> const std::map<std::filesystem::path,
                                                          BethesdaDirectory::BethesdaFile>&
{
    return m_fileMap;
}

std::vector<std::byte> BethesdaDirectory::file(const std::filesystem::path& relPath)
{
    // Find bsa/loose file to open.
    const BethesdaFile& file = fileFromMap(relPath);
    if (file.path.empty())
        throw std::runtime_error("File not found in file map");

    if (s_threadQueryObserver)
        s_threadQueryObserver->onGetFile(relPath, buildIdentity(file));

    std::vector<std::byte> outFileBytes;
    const std::shared_ptr<BSAFile> bsaStruct = file.bsaFile;
    if (!bsaStruct) {
        std::filesystem::path filePath;
        if (file.isGenerated)
            filePath = m_generatedDir / relPath;
        else
            filePath = m_dataDir / relPath;

        outFileBytes = FileUtil::fileBytes(filePath);
    } else {
        const std::filesystem::path bsaPath = bsaStruct->path;

        // This is a bsa archive file.
        const bsa::tes4::version& bsaVersion = bsaStruct->version;
        const bsa::tes4::archive& bsaObj = bsaStruct->archive;

        std::string parentPath = StringUtil::utf16toASCII(relPath.parent_path().wstring());
        std::string filename = StringUtil::utf16toASCII(relPath.filename().wstring());

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
    if (existingIt != m_fileMap.end() && !existingIt->second.isGenerated) {
        // Keep the original/native source so deletion of generated entries can restore it.
        m_generatedFileRestoreMap[relPath] = existingIt->second;
    }

    const BethesdaFile generatedFile
        = { .path = relPath, .bsaFile = nullptr, .isGenerated = true, .mtime = 0, .size = 0 };
    m_fileMap[relPath] = generatedFile;
}

void BethesdaDirectory::clearGeneratedFiles()
{
    const std::unique_lock lock(m_fileMapMutex);

    for (auto it = m_fileMap.begin(); it != m_fileMap.end();) {
        if (it->second.isGenerated) {
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

bool BethesdaDirectory::isLooseFile(const std::filesystem::path& relPath)
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");
    const BethesdaFile file = fileFromMap(relPath);
    return !file.path.empty() && !file.bsaFile;
}

bool BethesdaDirectory::isBSAFile(const std::filesystem::path& relPath)
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = fileFromMap(relPath);
    return !file.path.empty() && file.bsaFile;
}

bool BethesdaDirectory::isFile(const std::filesystem::path& relPath)
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = fileFromMap(relPath);
    const bool exists = !file.path.empty();

    if (s_threadQueryObserver)
        s_threadQueryObserver->onIsFile(relPath, exists, exists && file.isGenerated);

    return exists;
}

auto BethesdaDirectory::buildIdentity(const BethesdaFile& file) -> FileIdentity
{
    FileIdentity identity;

    if (file.path.empty()) {
        identity.kind = FileIdentity::Kind::None;
        return identity;
    }

    if (file.isGenerated) {
        identity.kind = FileIdentity::Kind::Generated;
        return identity;
    }

    if (file.bsaFile) {
        identity.kind = FileIdentity::Kind::BSA;
        identity.bsaRelPath = file.bsaFile->relPath.wstring();
        identity.bsaMtime = file.bsaFile->mtime;
        identity.bsaSize = file.bsaFile->size;
        return identity;
    }

    identity.kind = FileIdentity::Kind::Loose;
    identity.mtime = file.mtime;
    identity.size = file.size;
    return identity;
}

auto BethesdaDirectory::fileIdentity(const std::filesystem::path& relPath) -> FileIdentity
{
    const BethesdaFile file = fileFromMap(relPath);
    return buildIdentity(file);
}

void BethesdaDirectory::setThreadFileQueryObserver(FileQueryObserver* observer) { s_threadQueryObserver = observer; }

bool BethesdaDirectory::isGenerated(const std::filesystem::path& relPath)
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = fileFromMap(relPath);
    return !file.path.empty() && file.isGenerated;
}

std::filesystem::path BethesdaDirectory::looseFileFullPath(const std::filesystem::path& relPath)
{
    if (m_fileMap.empty())
        throw std::runtime_error("File map was not populated");

    const BethesdaFile file = fileFromMap(relPath);

    if (file.isGenerated)
        return m_generatedDir / relPath;

    return m_dataDir / relPath;
}

std::filesystem::path BethesdaDirectory::dataPath() const { return m_dataDir; }

std::filesystem::path BethesdaDirectory::generatedPath() const { return m_generatedDir; }

void BethesdaDirectory::addBSAFilesToMap()
{
    if (!m_bg)
        throw std::runtime_error("BethesdaGame object is not set which is required to load BSA files");

    Logger::info("Adding BSA files to file map.");

    // Get list of BSA files.
    const std::vector<std::wstring> bsaFiles = bsaLoadOrder();

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

    // Skip BSA if it doesn't exist (can happen if it's in the ini but not in the
    // data folder).
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

                if (!StringUtil::containsOnlyAscii(std::string(fileEntry.first.name()))
                    || !StringUtil::containsOnlyAscii(std::string(entry.first.name()))) {
                    Logger::warn(L"File {}\\{} in BSA {} contains non-ascii characters",
                                 StringUtil::windows1252toUTF16(std::string(fileEntry.first.name())),
                                 StringUtil::windows1252toUTF16(std::string(entry.first.name())),
                                 bsaName);

                    continue;
                }

                // Get folder name within the BSA vfs.
                const std::filesystem::path folderName = StringUtil::asciitoUTF16(std::string(fileEntry.first.name()));

                if (!m_foldersToMap.contains(folderName.begin()->wstring())) {
                    // Skip if folder is not in the list of folders to map.
                    continue;
                }

                // Get name of file.
                const std::wstring curEntry = StringUtil::asciitoUTF16(std::string(entry.first.name()));
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

std::vector<std::wstring> BethesdaDirectory::bsaLoadOrder() const
{
    // Get bsa files not loaded from esp (also initializes output vector).
    std::vector<std::wstring> outBSAOrder = bsaFilesFromINIs();

    // Get esp priority list.
    const std::vector<std::wstring> loadOrder = m_bg->activePlugins(true);

    // List BSA files in data directory.
    const std::vector<std::wstring> allBSAFiles = bsaFilesInDirectory();

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

std::filesystem::path BethesdaDirectory::modLookupFile(const std::filesystem::path& relPath)
{
    // Get file.
    const BethesdaFile& file = fileFromMap(relPath);
    if (file.bsaFile)
        return file.bsaFile->relPath;

    return relPath;
}

std::vector<std::wstring> BethesdaDirectory::bsaFilesFromINIs() const
{
    // Output vector.
    std::vector<std::wstring> bsaFiles;

    // Find ini paths.
    const BethesdaGame::ININame iniLocs = m_bg->iniPaths();

    std::vector<std::filesystem::path> iniFileOrder = { iniLocs.ini, iniLocs.iniCustom };

    // Find INIs in data folder.
    for (const auto& entry :
         std::filesystem::directory_iterator(m_dataDir, std::filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && StringUtil::toLowerASCII(entry.path().extension().wstring()) == L".ini")
            iniFileOrder.push_back(entry.path());
    }

    // Loop through each field.
    for (const auto& field : inibsaFields()) {
        // Loop through each ini file.
        std::wstring iniVal;
        for (const auto& iniPath : iniFileOrder) {
            const std::wstring curVal = readINIValue(iniPath, L"Archive", StringUtil::asciitoUTF16(field));
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

std::vector<std::wstring> BethesdaDirectory::bsaFilesInDirectory() const
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

std::vector<std::wstring> BethesdaDirectory::findBSAFilesFromPluginName(const std::vector<std::wstring>& bsaFileList,
                                                                        const std::wstring& pluginPrefix)
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

            if (!afterPrefix.starts_with(L' ') && (!isdigit(afterPrefix[0])))
                continue;

            bsaFilesFound.push_back(bsa);
        }
    }

    return bsaFilesFound;
}

bool BethesdaDirectory::isFileAllowed(const std::filesystem::path& filePath)
{
    std::wstring fileExtension = filePath.extension().wstring();
    boost::algorithm::to_lower(fileExtension);

    return !ContainerUtil::isInVector(extensionBlocklist(), fileExtension);
}

// Helpers.

bool BethesdaDirectory::isPathAscii(const std::filesystem::path& path)
{
    return std::ranges::all_of(path.wstring(), [](wchar_t wc) { return wc <= asciiUpperBound; });
}

auto BethesdaDirectory::fileFromMap(const std::filesystem::path& filePath) -> BethesdaDirectory::BethesdaFile
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const std::shared_lock lock(m_fileMapMutex);
    if (!m_fileMap.contains(filePath)) {
        return BethesdaFile {
            .path = std::filesystem::path(),
            .bsaFile = nullptr,
            .isGenerated = false,
            .mtime = 0,
            .size = 0,
        };
    }

    return m_fileMap.at(filePath);
}

void BethesdaDirectory::updateFileMap(const std::filesystem::path& filePath,
                                      std::shared_ptr<BethesdaDirectory::BSAFile> bsaFile,
                                      const bool& isGenerated,
                                      const int64_t& mtime,
                                      const uint64_t& size)
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const std::unique_lock lock(m_fileMapMutex);

    const BethesdaFile newBFile
        = { .path = filePath, .bsaFile = std::move(bsaFile), .isGenerated = isGenerated, .mtime = mtime, .size = size };

    m_fileMap[filePath] = newBFile;
}

bool BethesdaDirectory::isFileInBSA(const std::filesystem::path& file,
                                    const std::vector<std::wstring>& bsaFiles)
{
    if (isBSAFile(file)) {
        BethesdaFile const bethFile = fileFromMap(file);
        std::filesystem::path const bsaFilepath = bethFile.bsaFile->path.filename();
        const std::wstring bsaFilename = bsaFilepath.wstring();

        if (std::ranges::any_of(
                bsaFiles, [&bsaFilename](std::wstring const& file) { return boost::iequals(file, bsaFilename); })) {
            return true;
        }
    }
    return false;
}

std::vector<LPCWSTR> BethesdaDirectory::convertWStringToLPCWSTRVector(const std::vector<std::wstring>& original)
{
    std::vector<LPCWSTR> output(original.size());
    for (size_t i = 0; i < original.size(); i++)
        output[i] = original[i].c_str();

    return output;
}

bool BethesdaDirectory::checkGlob(const LPCWSTR& str,
                                  LPCWSTR& winningGlob,
                                  const std::vector<LPCWSTR>& globList)
{
    if (!boost::equals(winningGlob, L"") && PathMatchSpecW(str, winningGlob)) {
        // No winning glob, check all globs.
        return true;
    }

    for (const auto& glob : globList) {
        if (glob != winningGlob && PathMatchSpecW(str, glob)) {
            winningGlob = glob;
            return true;
        }
    }

    return false;
}

bool BethesdaDirectory::isHidden(const std::filesystem::path& path)
{
    // Check if file is hidden in filesystem.
    DWORD const fileAttributes = GetFileAttributesW(path.c_str());
    if (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_HIDDEN))
        return true;

    // Check if file is a dotfile.
    if (path.filename().wstring().starts_with(L'.'))
        return true;

    // Check if file ends in .mohidden (MO2 hidden file).
    if (boost::iequals(path.extension().wstring(), ".mohidden"))
        return true;

    return false;
}

std::wstring BethesdaDirectory::readINIValue(const std::filesystem::path& iniPath,
                                             const std::wstring& section,
                                             const std::wstring& key)
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
