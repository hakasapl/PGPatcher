#include "BethesdaDirectory.hpp"

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "PGDiag.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenUtil.hpp"

#include <bsa/tes4.hpp>

#include <shared_mutex>
#include <spdlog/spdlog.h>

#include <binary_io/any_stream.hpp>
#include <binary_io/memory_stream.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/crc.hpp>

#include <shlwapi.h>
#include <winnt.h>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>

using namespace std;
using namespace ParallaxGenUtil;

BethesdaDirectory::BethesdaDirectory(
    BethesdaGame* bg, filesystem::path generatedPath, ModManagerDirectory* mmd, const bool& logging)
    : m_generatedDir(std::move(generatedPath))
    , m_logging(logging)
    , m_bg(bg)
    , m_mmd(mmd)
{
    // Assign instance vars
    m_dataDir = filesystem::path(this->m_bg->getGameDataPath());

    if (this->m_logging) {
        // Log starting message
        spdlog::info(L"Opening Data Folder \"{}\"", m_dataDir.wstring());
    }
}

BethesdaDirectory::BethesdaDirectory(
    filesystem::path dataPath, filesystem::path generatedPath, ModManagerDirectory* mmd, const bool& logging)
    : m_dataDir(std::move(dataPath))
    , m_generatedDir(std::move(generatedPath))
    , m_logging(logging)
    , m_bg(nullptr)
    , m_mmd(mmd)
{
    if (this->m_logging) {
        // Log starting message
        spdlog::info(L"Opening Data Folder \"{}\"", m_dataDir.wstring());
    }
}

//
// Constant Definitions
//
auto BethesdaDirectory::getINIBSAFields() -> vector<string>
{
    // these fields will be searched in ini files for manually specified BSA
    // loading
    const static vector<string> iniBSAFields
        = { "sResourceArchiveList", "sResourceArchiveList2", "sResourceArchiveListBeta" };

    return iniBSAFields;
}

auto BethesdaDirectory::getExtensionBlocklist() -> vector<wstring>
{
    // any file that ends with these strings will be ignored
    // allowed BSAs etc. to be hidden from the file map since this object is an
    // abstraction of the data directory that no longer factors BSAs for
    // downstream users
    const static vector<wstring> extensionBlocklist = { L".bsa", L".esp", L".esl", L".esm" };

    return extensionBlocklist;
}

auto BethesdaDirectory::checkGlob(const wstring& str, const vector<wstring>& globList) -> bool
{
    // convert wstring vector to LPCWSTR vector
    vector<LPCWSTR> globListCstr = convertWStringToLPCWSTRVector(globList);

    // convert wstring to LPCWSTR
    LPCWSTR strCstr = str.c_str();

    // check if string matches any glob
    return std::ranges::any_of(globListCstr, [&](LPCWSTR glob) { return PathMatchSpecW(strCstr, glob); });
}

void BethesdaDirectory::populateFileMap(bool includeBSAs)
{
    const PGDiag::Prefix fileMapPrefix("fileMap", nlohmann::json::value_t::object);

    // clear map before populating
    {
        const unique_lock lock(m_fileMapMutex);
        m_fileMap.clear();
    }

    if (includeBSAs && m_bg != nullptr) {
        // add BSA files to file map
        addBSAFilesToMap();
    }

    // add loose files to file map
    addLooseFilesToMap();
}

auto BethesdaDirectory::getFileMap() const -> const map<filesystem::path, BethesdaDirectory::BethesdaFile>&
{
    return m_fileMap;
}

auto BethesdaDirectory::getFile(const filesystem::path& relPath) -> vector<std::byte>
{
    // find bsa/loose file to open
    const BethesdaFile& file = getFileFromMap(relPath);
    if (file.path.empty()) {
        if (m_logging) {
            spdlog::error(L"File not found in file map: {}", relPath.wstring());
        }
        throw runtime_error("File not found in file map");
    }

    vector<std::byte> outFileBytes;
    const shared_ptr<BSAFile> bsaStruct = file.bsaFile;
    if (bsaStruct == nullptr) {
        if (m_logging) {
            spdlog::trace(L"Reading loose file from BethesdaDirectory: {}", relPath.wstring());
        }

        filesystem::path filePath;
        if (file.generated) {
            filePath = m_generatedDir / relPath;
        } else {
            filePath = m_dataDir / relPath;
        }

        outFileBytes = getFileBytes(filePath);
    } else {
        const filesystem::path bsaPath = bsaStruct->path;

        if (m_logging) {
            spdlog::trace(L"Reading BSA file from {}: {}", bsaPath.wstring(), relPath.wstring());
        }

        // this is a bsa archive file
        const bsa::tes4::version& bsaVersion = bsaStruct->version;
        const bsa::tes4::archive& bsaObj = bsaStruct->archive;

        string parentPath = utf16toASCII(relPath.parent_path().wstring());
        string filename = utf16toASCII(relPath.filename().wstring());

        const auto& file = bsaObj[parentPath][filename];
        if (file) {
            binary_io::any_ostream aos { std::in_place_type<binary_io::memory_ostream> };
            // read file from output stream
            try {
                file->write(aos, bsaVersion);
            } catch (const std::exception& e) {
                if (m_logging) {
                    spdlog::error(L"Failed to read file {}: {}", relPath.wstring(), asciitoUTF16(e.what()));
                }
            }

            auto& s = aos.get<binary_io::memory_ostream>();
            outFileBytes = s.rdbuf();
        } else {
            throw runtime_error("File not found in BSA archive");
        }
    }

    if (outFileBytes.empty()) {
        return {};
    }

    return outFileBytes;
}

auto BethesdaDirectory::getMod(const filesystem::path& relPath) -> shared_ptr<ModManagerDirectory::Mod>
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    const BethesdaFile file = getFileFromMap(relPath);
    return file.mod;
}

void BethesdaDirectory::addGeneratedFile(const filesystem::path& relPath, std::shared_ptr<ModManagerDirectory::Mod> mod)
{
    updateFileMap(relPath, nullptr, std::move(mod), true);
}

auto BethesdaDirectory::isLooseFile(const filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }
    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.bsaFile == nullptr;
}

auto BethesdaDirectory::isBSAFile(const filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.bsaFile != nullptr;
}

auto BethesdaDirectory::isFile(const filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty();
}

auto BethesdaDirectory::isGenerated(const filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    const BethesdaFile file = getFileFromMap(relPath);
    return !file.path.empty() && file.generated;
}

auto BethesdaDirectory::isPrefix(const filesystem::path& relPath) -> bool
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    auto it = m_fileMap.lower_bound(relPath);
    if (it == m_fileMap.end()) {
        return false;
    }

    return boost::istarts_with(it->first.wstring(), relPath.wstring())
        || (it != m_fileMap.begin() && boost::istarts_with(prev(it)->first.wstring(), relPath.wstring()));
}

auto BethesdaDirectory::getLooseFileFullPath(const filesystem::path& relPath) -> filesystem::path
{
    if (m_fileMap.empty()) {
        throw runtime_error("File map was not populated");
    }

    const BethesdaFile file = getFileFromMap(relPath);

    if (file.generated) {
        return m_generatedDir / relPath;
    }

    return m_dataDir / relPath;
}

auto BethesdaDirectory::getDataPath() const -> filesystem::path { return m_dataDir; }

auto BethesdaDirectory::getGeneratedPath() const -> filesystem::path { return m_generatedDir; }

void BethesdaDirectory::addBSAFilesToMap()
{
    if (m_bg == nullptr) {
        throw runtime_error("BethesdaGame object is not set which is required to load BSA files");
    }

    if (m_logging) {
        spdlog::info("Adding BSA files to file map.");
    }

    // Get list of BSA files
    const vector<wstring> bsaFiles = getBSALoadOrder();

    // Loop through each BSA file
    for (const auto& bsaName : bsaFiles) {
        // add bsa to file map
        try {
            addBSAToFileMap(bsaName);
        } catch (const std::exception& e) {
            if (m_logging) {
                spdlog::error(L"Failed to add BSA file {} to map (Skipping): {}", bsaName, asciitoUTF16(e.what()));
            }
            continue;
        }
    }
}

void BethesdaDirectory::addLooseFilesToMap()
{
    if (m_logging) {
        spdlog::info("Adding loose files to file map.");
    }

    // loop through each folder to map
    for (const auto& folder : PGGlobals::s_foldersToMap) {
        // check if folder exists
        const auto curCheckFolder = m_dataDir / folder;
        if (!filesystem::exists(curCheckFolder)) {
            continue;
        }

        for (auto it = filesystem::recursive_directory_iterator(
                 curCheckFolder, filesystem::directory_options::skip_permission_denied);
            it != filesystem::recursive_directory_iterator(); ++it) {
            const auto entry = *it;

            if (isHidden(entry.path())) {
                if (entry.is_directory()) {
                    // If it's a directory, don't recurse into it
                    it.disable_recursion_pending();
                }
                continue;
            }

            const filesystem::path& filePath = entry.path();
            filesystem::path relativePath = filePath.lexically_relative(m_dataDir);
            relativePath = getAsciiPathLower(relativePath);

            // check type of file, skip BSAs and ESPs
            if (!isFileAllowed(filePath)) {
                continue;
            }

            if (m_logging) {
                spdlog::trace(L"Adding loose file to map: {}", relativePath.wstring());
            }

            shared_ptr<ModManagerDirectory::Mod> curMod;
            if (m_mmd != nullptr) {
                curMod = m_mmd->getModByFile(relativePath);
            }
            updateFileMap(relativePath, nullptr, curMod);
        }
    }
}

void BethesdaDirectory::addBSAToFileMap(const wstring& bsaName)
{
    if (m_logging) {
        // log message
        spdlog::debug(L"Adding files from {} to file map.", bsaName);
    }

    bsa::tes4::archive bsaObj;
    const filesystem::path bsaPath = m_dataDir / bsaName;

    // skip BSA if it doesn't exist (can happen if it's in the ini but not in the
    // data folder)
    if (!filesystem::exists(bsaPath)) {
        if (m_logging) {
            spdlog::warn(L"Skipping BSA {} because it doesn't exist", bsaPath.wstring());
        }
        return;
    }

    const bsa::tes4::version bsaVersion = bsaObj.read(bsaPath);
    const BSAFile bsaStruct = { .path = bsaPath, .version = bsaVersion, .archive = bsaObj };

    const shared_ptr<BSAFile> bsaStructPtr = make_shared<BSAFile>(bsaStruct);

    // loop iterator
    for (auto& fileEntry : bsaObj) {
        // get file entry from pointer
        try {
            // .second stores the files in the folder
            const auto fileName = fileEntry.second;

            // loop through files in folder
            for (const auto& entry : fileName) {

                if (!containsOnlyAscii(string(fileEntry.first.name()))
                    || !containsOnlyAscii(string(entry.first.name()))) {
                    spdlog::warn(L"File {}\\{} in BSA {} contains non-ascii characters which is not handled correctly "
                                 L"by Skyrim - skipping",
                        windows1252toUTF16(string(fileEntry.first.name())),
                        windows1252toUTF16(string(entry.first.name())), bsaName);

                    continue;
                }

                // get folder name within the BSA vfs
                const filesystem::path folderName = asciitoUTF16(string(fileEntry.first.name()));

                if (!PGGlobals::s_foldersToMap.contains(folderName.begin()->wstring())) {
                    // skip if folder is not in the list of folders to map
                    continue;
                }

                // get name of file
                const wstring curEntry = asciitoUTF16(string(entry.first.name()));
                filesystem::path curPath = folderName / curEntry;
                curPath = getAsciiPathLower(curPath);

                // chekc if we should ignore this file
                if (!isFileAllowed(curPath)) {
                    continue;
                }

                if (m_logging) {
                    spdlog::trace(L"Adding file from BSA {} to file map: {}", bsaName, curPath.wstring());
                }

                // add to filemap
                shared_ptr<ModManagerDirectory::Mod> bsaMod;
                if (m_mmd != nullptr) {
                    bsaMod = m_mmd->getModByFile(bsaName);
                }
                updateFileMap(curPath, bsaStructPtr, bsaMod);
            }
        } catch (const std::exception& e) {
            if (m_logging) {
                spdlog::error(L"Failed to get file pointer from BSA, skipping {}: {}", bsaName, asciitoUTF16(e.what()));
            }
            continue;
        }
    }
}

auto BethesdaDirectory::getBSALoadOrder() const -> vector<wstring>
{
    // get bsa files not loaded from esp (also initializes output vector)
    vector<wstring> outBSAOrder = getBSAFilesFromINIs();

    // get esp priority list
    const vector<wstring> loadOrder = m_bg->getActivePlugins(true);

    // list BSA files in data directory
    const vector<wstring> allBSAFiles = getBSAFilesInDirectory();

    // loop through each esp in the priority list
    for (const auto& plugin : loadOrder) {
        // add any BSAs to list
        const vector<wstring> curFoundBSAs = findBSAFilesFromPluginName(allBSAFiles, plugin);
        concatenateVectorsWithoutDuplicates(outBSAOrder, curFoundBSAs);
    }

    if (m_logging) {
        // log output
        wstring bsaListStr = boost::algorithm::join(outBSAOrder, ",");
        spdlog::trace(L"BSA Load Order: {}", bsaListStr);

        for (const auto& bsa : allBSAFiles) {
            if (!isInVector(outBSAOrder, bsa)) {
                spdlog::warn(L"BSA file {} not loaded by any active plugin or INI.", bsa);
            }
        }
    }

    return outBSAOrder;
}

auto BethesdaDirectory::getAsciiPathLower(const filesystem::path& path) -> filesystem::path
{
    if (!isPathAscii(path)) {
        spdlog::debug(
            L"Trying to convert unicode path {} to lower case but only ASCII characters are converted", path.wstring());
    }
    return { boost::to_lower_copy(path.wstring(), std::locale::classic()) };
}

auto BethesdaDirectory::pathEqualityIgnoreCase(const filesystem::path& path1, const filesystem::path& path2) -> bool
{
    return getAsciiPathLower(path1) == getAsciiPathLower(path2);
}

auto BethesdaDirectory::getBSAFilesFromINIs() const -> vector<wstring>
{
    // output vector
    vector<wstring> bsaFiles;

    if (m_logging) {
        spdlog::debug("Reading manually loaded BSAs from INI files.");
    }

    // find ini paths
    const BethesdaGame::ININame iniLocs = m_bg->getINIPaths();

    vector<filesystem::path> iniFileOrder = { iniLocs.ini, iniLocs.iniCustom };

    // Find INIs in data folder
    for (const auto& entry :
        filesystem::directory_iterator(m_dataDir, filesystem::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file() && toLowerASCII(entry.path().extension().wstring()) == L".ini") {
            iniFileOrder.push_back(entry.path());
        }
    }

    // loop through each field
    bool firstINIRead = true;
    for (const auto& field : getINIBSAFields()) {
        // loop through each ini file
        wstring iniVal;
        for (const auto& iniPath : iniFileOrder) {
            wstring curVal = readINIValue(iniPath, L"Archive", asciitoUTF16(field), m_logging, firstINIRead);

            if (m_logging) {
                spdlog::trace(
                    L"Found ini key pair from INI {}: {}: {}", iniPath.wstring(), asciitoUTF16(field), curVal);
            }

            if (curVal.empty()) {
                continue;
            }

            iniVal = curVal;
        }

        firstINIRead = false;

        if (iniVal.empty()) {
            continue;
        }

        if (m_logging) {
            spdlog::trace(L"Found BSA files from INI field {}: {}", asciitoUTF16(field), iniVal);
        }

        // split into components
        vector<wstring> iniComponents;
        boost::split(iniComponents, iniVal, boost::is_any_of(","));
        for (auto& bsa : iniComponents) {
            // remove leading/trailing whitespace
            boost::trim(bsa);

            // add to output
            addUniqueElement(bsaFiles, bsa);
        }
    }

    return bsaFiles;
}

auto BethesdaDirectory::getBSAFilesInDirectory() const -> vector<wstring>
{
    if (m_logging) {
        spdlog::debug("Finding existing BSA files in data directory.");
    }

    vector<wstring> bsaFiles;

    for (const auto& entry : filesystem::directory_iterator(this->m_dataDir)) {
        if (entry.is_regular_file()) {
            const auto fileExtension = entry.path().extension().string();
            // only interested in BSA files
            if (!boost::iequals(fileExtension, ".bsa")) {
                continue;
            }

            // add to output
            bsaFiles.push_back(entry.path().filename().wstring());
        }
    }

    return bsaFiles;
}

auto BethesdaDirectory::findBSAFilesFromPluginName(
    const vector<wstring>& bsaFileList, const wstring& pluginPrefix) const -> vector<wstring>
{
    if (m_logging) {
        spdlog::trace(L"Finding BSA files that correspond to plugin {}", pluginPrefix);
    }

    vector<wstring> bsaFilesFound;
    const wstring pluginPrefixLower = boost::to_lower_copy(pluginPrefix);

    for (wstring bsa : bsaFileList) {
        const wstring bsaLower = boost::to_lower_copy(bsa);
        if (bsaLower.starts_with(pluginPrefixLower)) {
            if (bsaLower == pluginPrefixLower + L".bsa") {
                // load bsa with the plugin name before any others
                bsaFilesFound.insert(bsaFilesFound.begin(), bsa);
                continue;
            }

            // skip any BSAs that may start with the prefix but belong to a different
            // plugin
            wstring afterPrefix = bsa.substr(pluginPrefix.length());

            // todo: Is this actually how the game handles BSA files? Example:
            // 3DNPC0.bsa, 3DNPC1.bsa, 3DNPC2.bsa are loaded, todo: but 3DNPC -
            // Textures.bsa is also loaded, whats the logic there?
            if (afterPrefix.starts_with(L" ") && !afterPrefix.starts_with(L" -")) {
                continue;
            }

            if (!afterPrefix.starts_with(L" ") && (isdigit(afterPrefix[0]) == 0)) {
                continue;
            }

            if (m_logging) {
                spdlog::trace(L"Found BSA file that corresponds to plugin {}: {}", pluginPrefix, bsa);
            }

            bsaFilesFound.push_back(bsa);
        }
    }

    return bsaFilesFound;
}

auto BethesdaDirectory::isFileAllowed(const filesystem::path& filePath) -> bool
{
    wstring fileExtension = filePath.extension().wstring();
    boost::algorithm::to_lower(fileExtension);

    return !(isInVector(getExtensionBlocklist(), fileExtension));
}

// helpers

auto BethesdaDirectory::isPathAscii(const filesystem::path& path) -> bool
{
    return ranges::all_of(path.wstring(), [](wchar_t wc) { return wc <= ASCII_UPPER_BOUND; });
}

auto BethesdaDirectory::getFileFromMap(const filesystem::path& filePath) -> BethesdaDirectory::BethesdaFile
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const shared_lock lock(m_fileMapMutex);
    if (!m_fileMap.contains(filePath)) {
        return BethesdaFile { .path = filesystem::path(), .bsaFile = nullptr };
    }

    return m_fileMap.at(filePath);
}

void BethesdaDirectory::updateFileMap(const filesystem::path& filePath, shared_ptr<BethesdaDirectory::BSAFile> bsaFile,
    std::shared_ptr<ModManagerDirectory::Mod> mod, const bool& generated)
{
    // const filesystem::path lowerPath = getAsciiPathLower(filePath);

    const unique_lock lock(m_fileMapMutex);

    const BethesdaFile newBFile
        = { .path = filePath, .bsaFile = std::move(bsaFile), .mod = std::move(mod), .generated = generated };

    m_fileMap[filePath] = newBFile;

    PGDiag::insert(filePath.wstring(), newBFile.getDiagJSON());
}

auto BethesdaDirectory::isFileInBSA(const filesystem::path& file, const std::vector<std::wstring>& bsaFiles) -> bool
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

auto BethesdaDirectory::checkIfAnyComponentIs(const filesystem::path& path, const vector<wstring>& components) -> bool
{
    for (const auto& component : path) {
        for (const auto& comp : components) {
            if (boost::iequals(component.wstring(), comp)) {
                return true;
            }
        }
    }

    return false;
}

auto BethesdaDirectory::convertWStringToLPCWSTRVector(const vector<wstring>& original) -> vector<LPCWSTR>
{
    vector<LPCWSTR> output(original.size());
    for (size_t i = 0; i < original.size(); i++) {
        output[i] = original[i].c_str();
    }

    return output;
}

auto BethesdaDirectory::checkGlob(const LPCWSTR& str, LPCWSTR& winningGlob, const vector<LPCWSTR>& globList) -> bool
{
    if (!boost::equals(winningGlob, L"") && (PathMatchSpecW(str, winningGlob) != 0)) {
        // no winning glob, check all globs
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

auto BethesdaDirectory::isHidden(const filesystem::path& path) -> bool
{
    // check if file is hidden in filesystem
    DWORD const fileAttributes = GetFileAttributesW(path.c_str());
    if (fileAttributes != INVALID_FILE_ATTRIBUTES && (fileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0) {
        return true;
    }

    // check if file is a dotfile
    if (path.filename().wstring().starts_with(L".")) {
        return true;
    }

    // check if file ends in .mohidden (MO2 hidden file)
    if (boost::iequals(path.extension().wstring(), ".mohidden")) {
        return true;
    }

    return false;
}

auto BethesdaDirectory::readINIValue(const filesystem::path& iniPath, const wstring& section, const wstring& key,
    const bool& logging, const bool& firstINIRead) -> wstring
{
    if (!filesystem::exists(iniPath)) {
        if (logging && firstINIRead) {
            spdlog::warn(L"INI file does not exist (ignoring): {}", iniPath.wstring());
        }
        return L"";
    }

    ifstream f(iniPath);
    if (!f.is_open()) {
        if (logging && firstINIRead) {
            spdlog::warn(L"Unable to open INI (ignoring): {}", iniPath.wstring());
        }
        return L"";
    }

    string curLine;
    string curSection;
    bool foundSection = false;

    while (getline(f, curLine)) {
        boost::trim(curLine);

        // ignore comments
        if (curLine.empty() || curLine[0] == ';' || curLine[0] == '#') {
            continue;
        }

        // Check if it's a section
        if (curLine.front() == '[' && curLine.back() == ']') {
            curSection = curLine.substr(1, curLine.size() - 2);
            continue;
        }

        // Check if it's the correct section
        if (boost::iequals(curSection, section)) {
            foundSection = true;
        }

        if (!boost::iequals(curSection, section)) {
            // exit if already checked section
            if (foundSection) {
                break;
            }
            continue;
        }

        // check key
        const size_t pos = curLine.find('=');
        if (pos != std::string::npos) {
            // found key value pair
            string curKey = curLine.substr(0, pos);
            boost::trim(curKey);
            if (boost::iequals(curKey, key)) {
                string curValue = curLine.substr(pos + 1);
                boost::trim(curValue);
                return ParallaxGenUtil::utf8toUTF16(curValue);
            }
        }
    }

    return L"";
}
