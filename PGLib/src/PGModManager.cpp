#include "PGModManager.hpp"

#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "common/MO2DataFolder.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ranges>
#include <regex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>
#include <tlhelp32.h>

uint8_t PGModManager::fromHexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f')
        return static_cast<uint8_t>(hexAlphaBase + (c - 'a'));
    if (c >= 'A' && c <= 'F')
        return static_cast<uint8_t>(hexAlphaBase + (c - 'A'));
    return 0;
}

std::wstring PGModManager::decodeQtByteArrayValue(const std::string& byteArrayVal)
{
    // Qt may serialize QByteArray values with C-style escapes like "\\xC3\\xA0".
    std::string decodedBytes;
    decodedBytes.reserve(byteArrayVal.size());

    for (size_t i = 0; i < byteArrayVal.size();) {
        const auto curCh = byteArrayVal.at(i);
        if (curCh == '\\' && i + 1 < byteArrayVal.size()) {
            const auto nextCh = byteArrayVal.at(i + 1);
            if (nextCh == 'x' && i + 3 < byteArrayVal.size()) {
                const auto hiCh = byteArrayVal.at(i + 2);
                const auto loCh = byteArrayVal.at(i + 3);
                if (std::isxdigit(static_cast<unsigned char>(hiCh))
                    && std::isxdigit(static_cast<unsigned char>(loCh))) {
                    const auto hi = fromHexDigit(hiCh);
                    const auto lo = fromHexDigit(loCh);
                    decodedBytes.push_back(static_cast<char>((hi << 4U) | lo));
                    i += 4;
                    continue;
                }
            }

            // Preserve common escaped backslashes and pass through other escapes as literals.
            if (nextCh == '\\') {
                decodedBytes.push_back('\\');
                i += 2;
                continue;
            }
        }

        decodedBytes.push_back(curCh);
        ++i;
    }

    return StringUtil::utf8toUTF16(decodedBytes);
}

PGModManager::PGModManager(const ModManagerType& mmType)
    : m_mmType(mmType)
{
}

auto PGModManager::modFileMap() const -> const std::unordered_map<std::filesystem::path,
                                                                  std::shared_ptr<Mod>>&
{
    return m_modFileMap;
}

auto PGModManager::modByFile(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>
{
    const auto it = m_modFileMap.find(relPath);
    if (it != m_modFileMap.end())
        return it->second;

    return nullptr;
}

auto PGModManager::modByFileSmart(const std::filesystem::path& relPath) const -> std::shared_ptr<Mod>
{
    // Accounts for files in BSAs.

    // Get mod searchable file from PGD.
    auto* pgd = PGGlobals::pgd();
    if (!pgd)
        throw std::runtime_error("PGD is null");

    const auto modSearchableFile = pgd->modLookupFile(relPath);

    auto mod = modByFile(modSearchableFile);

    // Record lookup for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunObserver::current().recordModOfFile(relPath, !mod ? std::wstring() : mod->name);

    return mod;
}

auto PGModManager::mods() const -> std::vector<std::shared_ptr<Mod>>
{
    std::vector<std::shared_ptr<Mod>> mods;
    for (const auto& [modName, mod] : m_modMap) {
        if (modName.empty())
            continue; // skip empty mod

        mods.push_back(mod);
    }

    return mods;
}

auto PGModManager::modsByPriority() const -> std::vector<std::shared_ptr<Mod>>
{
    std::vector<std::shared_ptr<Mod>> mods = this->mods();

    // Sort mods by priority (higher priority first), then by modManagerOrder (lower order first), then by name.
    std::ranges::stable_sort(mods, [](const auto& a, const auto& b) { return PGModManager::compareMods(a, b, true); });

    return mods;
}

auto PGModManager::modsByDefaultOrder() const -> std::vector<std::shared_ptr<Mod>>
{
    std::vector<std::shared_ptr<Mod>> mods = this->mods();

    // Sort mods by modManagerOrder (lower order first), then by name.
    std::ranges::stable_sort(mods, [](const auto& a, const auto& b) { return PGModManager::compareMods(a, b, false); });

    return mods;
}

auto PGModManager::mod(const std::wstring& modName) const -> std::shared_ptr<Mod>
{
    if (m_modMap.contains(modName))
        return m_modMap.at(modName);

    return nullptr;
}

void PGModManager::loadJSON(const nlohmann::json& json)
{
    if (!json.is_object())
        throw std::runtime_error("JSON is not an object");

    m_didLoadModRules = true;

    for (const auto& [modName, properties] : json.items()) {
        if (modName.empty())
            continue;

        if (!properties.is_object())
            throw std::runtime_error("JSON mod properties is not an object");

        int priority = -1;
        if (properties.contains("priority")) {
            if (!properties["priority"].is_number_integer())
                throw std::runtime_error("JSON mod priority is not an integer");

            priority = properties["priority"].get<int>();
        }

        bool isEnabled = false;
        if (properties.contains("enabled")) {
            if (!properties["enabled"].is_boolean())
                throw std::runtime_error("JSON mod enabled is not a boolean");

            isEnabled = properties["enabled"].get<bool>();
        }

        bool areMeshesIgnored = false;
        if (properties.contains("meshesignored")) {
            if (!properties["meshesignored"].is_boolean())
                throw std::runtime_error("JSON mod meshesignored is not a boolean");

            areMeshesIgnored = properties["meshesignored"].get<bool>();
        }

        std::shared_ptr<Mod> modPtr = nullptr;
        const auto modNameWStr = StringUtil::utf8toUTF16(modName);
        if (!m_modMap.contains(modNameWStr)) {
            // Create mod if it doesn't exist.
            modPtr = std::make_shared<Mod>();
            modPtr->name = modNameWStr;
            m_modMap[modNameWStr] = modPtr;
        } else {
            modPtr = m_modMap.at(modNameWStr);
        }

        modPtr->isNew = false;
        modPtr->priority = priority;
        modPtr->isEnabled = isEnabled;
        modPtr->areMeshesIgnored = areMeshesIgnored;
    }
}

bool PGModManager::hasLoadedModRules() const { return m_didLoadModRules; }

nlohmann::json PGModManager::json()
{
    nlohmann::json json = nlohmann::json::object();

    for (const auto& [modName, mod] : m_modMap) {
        const auto utf8ModName = StringUtil::utf16toUTF8(modName);
        json[utf8ModName] = nlohmann::json::object();
        json[utf8ModName]["priority"] = mod->priority;
        json[utf8ModName]["enabled"] = mod->isEnabled;
        json[utf8ModName]["meshesignored"] = mod->areMeshesIgnored;
    }

    return json;
}

void PGModManager::populateModFileMapVortex(const std::filesystem::path& deploymentDir)
{
    // Required file is vortex.deployment.json in the data folder.
    Logger::info("Populating mods from Vortex");

    const auto deploymentFile = deploymentDir / vortexDeploymentFileName;

    if (!std::filesystem::exists(deploymentFile)) {
        throw std::runtime_error("Vortex deployment file does not exist: "
                                 + StringUtil::utf16toUTF8(deploymentFile.wstring()));
    }

    std::ifstream vortexDepFileF(deploymentFile);
    nlohmann::json vortexDeployment = nlohmann::json::parse(vortexDepFileF);
    vortexDepFileF.close();

    // Check that files field exists.
    if (!vortexDeployment.contains("files")) {
        throw std::runtime_error("Vortex deployment file does not contain 'files' field: "
                                 + StringUtil::utf16toUTF8(deploymentFile.wstring()));
    }

    // Extract staging path where all Vortex mods are stored.
    if (!vortexDeployment.contains("stagingPath")) {
        throw std::runtime_error("Vortex deployment file does not contain 'stagingPath' field: "
                                 + StringUtil::utf16toUTF8(deploymentFile.wstring()));
    }

    const auto stagingPath
        = std::filesystem::path(StringUtil::utf8toUTF16(vortexDeployment["stagingPath"].get<std::string>()));
    m_stagingLocation = stagingPath;

    // Loop through files.
    std::unordered_set<std::wstring> foundMods;
    for (const auto& file : vortexDeployment["files"]) {
        const auto relPath = std::filesystem::path(StringUtil::utf8toUTF16(file["relPath"].get<std::string>()));

        // Check if relPath is within s_foldersToMap.
        // Get the first path component of relPath.
        const auto relPathStr = boost::to_lower_copy(relPath.begin()->wstring());
        if (!PGGlobals::s_foldersToMap.contains(relPathStr)) {
            // Skip if not mapping from this folder.
            continue;
        }

        // Get the mod identifier from source field (e.g., "1DwemerArmorSE-81043-1-1671541249").
        const auto sourceId = StringUtil::utf8toUTF16(file["source"].get<std::string>());
        const auto curModDir = stagingPath / sourceId;

        // Check if mod folder exists.
        if (!std::filesystem::exists(curModDir)) {
            Logger::debug(L"Mod directory from vortex.deployment.json does not exist: {}", curModDir.wstring());
            continue;
        }

        auto modName = sourceId;

        // Filter out modname suffix (e.g., remove "-81043-1-1671541249" from "1DwemerArmorSE-81043-1-1671541249").
        const static std::wregex vortexSuffixRe(L"-[0-9]+-.*");
        modName = std::regex_replace(modName, vortexSuffixRe, L"");

        std::shared_ptr<Mod> modPtr = nullptr;
        if (m_modMap.contains(modName)) {
            // Skip if already in map.
            modPtr = m_modMap.at(modName);
        } else {
            modPtr = std::make_shared<Mod>();
            modPtr->name = modName;
            modPtr->isNew = true;
            modPtr->priority = -1;
        }

        modPtr->modManagerOrder = 0; // Vortex does not have a mod manager order system by default
        modPtr->folder = curModDir; // Store the actual mod folder path

        // Update file map.
        Logger::trace(L"Mapping file to mod: {} -> {}", relPath.wstring(), modName);

        m_modMap[modName] = modPtr;
        foundMods.insert(modName);
        m_modFileMap[StringUtil::toLowerASCII(relPath.wstring())] = modPtr;
    }

    // Delete any mods from file map that were not found.
    for (auto it = m_modMap.begin(); it != m_modMap.end();)
        if (!foundMods.contains(it->second->name))
            it = m_modMap.erase(it);
        else
            ++it;
}

void PGModManager::populateModFileMapMO2(const std::filesystem::path& instanceDir,
                                         const std::filesystem::path& outputDir)
{
    // Required file is modlist.txt in the profile folder.

    Logger::info("Populating mods from Mod Organizer 2");

    // First read modorganizer.ini in the instance folder to get the profiles and mods folders.
    const std::filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!std::filesystem::exists(mo2IniFile)) {
        throw std::runtime_error("Mod Organizer 2 ini file does not exist: "
                                 + StringUtil::utf16toUTF8(mo2IniFile.wstring()));
    }

    const auto [profileDir, modDir] = mo2FilePaths(instanceDir);

    m_stagingLocation = modDir;

    // Find location of modlist.txt.
    const auto curProfile = selectedProfileFromInstanceDir(instanceDir);
    const auto modListFile = profileDir / curProfile / "modlist.txt";

    bool foundOneMod = false;

    // Loop through modlist.txt.
    int basePriority = 0;
    std::unordered_set<std::wstring> foundMods;
    for (const auto& mod : mo2EnabledMods(modListFile)) {
        // Loop through all files in mod.
        const auto curModDir = modDir / mod;

        // Check if mod folder exists.
        if (!std::filesystem::exists(curModDir)) {
            Logger::debug(L"Mod directory from modlist.txt does not exist: {}", curModDir.wstring());
            Logger::critical("modlist.txt from MO2 does not reflect the contents of the mods folder. This should not "
                             "happen unless MO2 is in a corrupt state.");
            return;
        }

        // Check if mod dir is output dir. The output dir may not exist yet, which is the one error that is ignored; any
        // other failure to compare the two is reported like before.
        std::error_code ec;
        const bool isOutputDir = std::filesystem::equivalent(curModDir, outputDir, ec);
        if (ec && std::filesystem::exists(outputDir)) {
            throw std::filesystem::filesystem_error(
                "Unable to compare mod directory with output directory", curModDir, outputDir, ec);
        }

        if (isOutputDir) {
            Logger::critical(L"If outputting to MO2 you must disable the mod {} first to prevent issues with MO2 VFS",
                             mod);
            return;
        }

        foundOneMod = true;

        std::shared_ptr<Mod> modPtr = nullptr;
        if (m_modMap.contains(mod)) {
            // Skip if already in map.
            modPtr = m_modMap.at(mod);
        } else {
            modPtr = std::make_shared<Mod>();
            modPtr->name = mod;
            modPtr->isNew = true;
            modPtr->priority = -1;
        }

        modPtr->modManagerOrder = basePriority++;
        modPtr->folder = curModDir;
        foundMods.insert(mod);

        m_modMap[mod] = modPtr;
        for (const auto& folder : PGGlobals::s_foldersToMap) {
            const auto curSearchDir = curModDir / folder;
            if (!std::filesystem::exists(curSearchDir)) {
                // Skip if folder doesn't exist.
                continue;
            }

            try {
                // The directory listing already says whether an entry is hidden and whether it is a file, and the path
                // relative to the mod folder follows from the walk, so no file has to be queried on its own.
                const std::wstring folderPrefix = (folder / L"").wstring();
                FileUtil::walkDirectory(
                    curSearchDir,
                    [this, &folderPrefix, &mod, &modPtr](const std::wstring& relPath,
                                                         const FileUtil::DirectoryEntry& entry) {
                        if (BethesdaDirectory::isHidden(entry)) {
                            // If it's a directory, don't recurse into it.
                            return false;
                        }

                        if (!entry.isRegularFile)
                            return true;

                        // Skip meta.ini file.
                        if (boost::iequals(entry.name, L"meta.ini"))
                            return true;

                        const std::filesystem::path relPathLower = StringUtil::toLowerASCII(folderPrefix + relPath);
                        // Check if already in map.
                        if (m_modFileMap.contains(relPathLower))
                            return true;

                        Logger::trace(L"Mapping file to mod: {} -> {}", relPathLower.wstring(), mod);

                        m_modFileMap[relPathLower] = modPtr;
                        return true;
                    });
            } catch (const std::filesystem::filesystem_error& e) {
                Logger::error(L"Error reading mod directory {}: {}", mod, StringUtil::asciitoUTF16(e.what()));
            }
        }

        // Map any BSAs.
        for (const auto& entry : FileUtil::listDirectory(curModDir)) {
            if (entry.isRegularFile
                && boost::iequals(std::filesystem::path(entry.name).extension().wstring(), ".bsa")) {
                const std::filesystem::path relPathLower = StringUtil::toLowerASCII(entry.name);
                // Check if already in map.
                if (m_modFileMap.contains(relPathLower))
                    continue;

                Logger::trace(L"Mapping file to mod: {} -> {}", relPathLower.wstring(), mod);

                m_modFileMap[relPathLower] = modPtr;
            }
        }
    }

    // Delete any mods from file map that were not found.
    for (auto it = m_modMap.begin(); it != m_modMap.end();)
        if (!foundMods.contains(it->second->name))
            it = m_modMap.erase(it);
        else
            ++it;

    if (!foundOneMod) {
        Logger::critical(L"MO2 modlist.txt was empty, no mods found");
        return;
    }
}

void PGModManager::populateModFileMapMO2(const MO2DataFolder& view,
                                         const std::filesystem::path& outputDir)
{
    Logger::info("Populating mods from Mod Organizer 2");

    m_stagingLocation = view.modsDir();

    // The mods of the view, in its order; the other sources (the overwrite folder, the data folder) own no file.
    std::vector<std::shared_ptr<Mod>> modsBySource(view.sources().size());
    std::unordered_set<std::wstring> foundMods;
    bool didFindMod = false;
    for (std::size_t sourceIndex = 0; sourceIndex < view.sources().size(); ++sourceIndex) {
        const auto& source = view.sources()[sourceIndex];
        if (source.kind != MO2DataFolder::SourceKind::Mod)
            continue;

        // Check if mod dir is output dir. The output dir may not exist yet, which is the one error that is ignored; any
        // other failure to compare the two is reported like before.
        std::error_code ec;
        const bool isOutputDir = std::filesystem::equivalent(source.folder, outputDir, ec);
        if (ec && std::filesystem::exists(outputDir)) {
            throw std::filesystem::filesystem_error(
                "Unable to compare mod directory with output directory", source.folder, outputDir, ec);
        }

        if (isOutputDir) {
            Logger::critical(L"If outputting to MO2 you must disable the mod {} first to prevent issues with MO2 VFS",
                             source.name);
            return;
        }

        didFindMod = true;

        std::shared_ptr<Mod> modPtr = nullptr;
        if (m_modMap.contains(source.name)) {
            // Skip if already in map.
            modPtr = m_modMap.at(source.name);
        } else {
            modPtr = std::make_shared<Mod>();
            modPtr->name = source.name;
            modPtr->isNew = true;
            modPtr->priority = -1;
        }

        modPtr->modManagerOrder = source.order;
        modPtr->folder = source.folder;
        foundMods.insert(source.name);

        m_modMap[source.name] = modPtr;
        modsBySource[sourceIndex] = modPtr;
    }

    // The files, as the walk of the mod folders maps them: those of the folders to map without meta.ini, and the
    // archives at the top level, each owned by the first mod with a copy the walk maps (the view left hidden copies
    // out of the ownership).
    for (const auto& file : view.files()) {
        if (file.ownerSource == MO2DataFolder::noSource)
            continue;
        const auto& modPtr = modsBySource[file.ownerSource];
        if (!modPtr)
            continue;

        const std::filesystem::path relPath(file.relPath);
        const bool isTopLevel = !relPath.has_parent_path();
        if (isTopLevel) {
            if (!boost::iequals(relPath.extension().wstring(), L".bsa"))
                continue;
        } else {
            if (boost::iequals(relPath.filename().wstring(), L"meta.ini"))
                continue;
            if (!PGGlobals::s_foldersToMap.contains(StringUtil::toLowerASCII(relPath.begin()->wstring())))
                continue;
        }

        const std::filesystem::path relPathLower = StringUtil::toLowerASCII(file.relPath);
        // Check if already in map.
        if (m_modFileMap.contains(relPathLower))
            continue;

        Logger::trace(L"Mapping file to mod: {} -> {}", relPathLower.wstring(), modPtr->name);

        m_modFileMap[relPathLower] = modPtr;
    }

    // Delete any mods from file map that were not found.
    for (auto it = m_modMap.begin(); it != m_modMap.end();)
        if (!foundMods.contains(it->second->name))
            it = m_modMap.erase(it);
        else
            ++it;

    if (!didFindMod)
        Logger::critical(L"MO2 modlist.txt lists no enabled mod with a folder, no mods found");
}

std::vector<std::wstring> PGModManager::mo2EnabledMods(const std::filesystem::path& modListFile)
{
    if (!std::filesystem::exists(modListFile)) {
        throw std::runtime_error("Mod Organizer 2 modlist.txt file does not exist: "
                                 + StringUtil::utf16toUTF8(modListFile.wstring()));
    }

    std::vector<std::wstring> mods;
    std::ifstream modListFileF(modListFile);
    std::string modStr;
    while (getline(modListFileF, modStr)) {
        std::wstring mod = StringUtil::utf8toUTF16(modStr);
        if (mod.empty()) {
            // Skip empty lines.
            continue;
        }

        if (mod.starts_with(L'-') || mod.starts_with(L'*')) {
            // Skip disabled and uncontrolled mods.
            continue;
        }

        if (mod.starts_with(L'#')) {
            // Skip comments.
            continue;
        }

        if (mod.ends_with(L"_separator")) {
            // Skip separators.
            continue;
        }

        mod.erase(0, 1); // remove +
        mods.push_back(std::move(mod));
    }

    return mods;
}

auto PGModManager::detect(const std::filesystem::path& dataFolder,
                          bool shouldLog) -> Detection
{
    const auto mo2Dir = mo2DirFromUSVFS();
    return detect(dataFolder, mo2Dir, mo2Dir.empty() ? std::filesystem::path() : runningMO2InstanceDir(), shouldLog);
}

auto PGModManager::detect(const std::filesystem::path& dataFolder,
                          const std::filesystem::path& mo2Dir,
                          const std::filesystem::path& runningInstanceDir,
                          bool shouldLog) -> Detection
{
    Detection detection;

    if (!mo2Dir.empty()) {
        detection.type = ModManagerType::ModOrganizer2;
        detection.mo2InstanceDir = runningInstanceDir;
        if (!shouldLog)
            return detection;

        if (runningInstanceDir.empty()) {
            Logger::warn("Mod Organizer 2 detected, but the instance that launched the program was not found: the "
                         "instance folder has to be set");
        } else {
            Logger::info(L"Mod Organizer 2 detected, instance {}", runningInstanceDir.wstring());
        }
        return detection;
    }

    std::error_code ec;
    if (!dataFolder.empty() && std::filesystem::exists(dataFolder / vortexDeploymentFileName, ec)) {
        detection.type = ModManagerType::Vortex;
        if (shouldLog)
            Logger::info("Vortex deployment detected");
        return detection;
    }

    if (shouldLog)
        Logger::info("No mod manager detected");
    return detection;
}

bool PGModManager::matchesActivePlugins(const std::filesystem::path& instanceDir,
                                        const std::filesystem::path& pluginsFile)
{
    try {
        // An instance that cannot be read is not compared (reading it for the mod list fails on its own).
        if (!isValidMO2InstanceDir(instanceDir))
            return true;

        const auto [profilesDir, modsDir] = mo2FilePaths(instanceDir);
        const auto profilePluginsFile = profilesDir / selectedProfileFromInstanceDir(instanceDir) / L"plugins.txt";

        std::error_code ec;
        if (!std::filesystem::is_regular_file(profilePluginsFile, ec)
            || !std::filesystem::is_regular_file(pluginsFile, ec)) {
            return true;
        }

        return FileUtil::fileBytes(profilePluginsFile) == FileUtil::fileBytes(pluginsFile);
    } catch (const std::exception&) {
        return true;
    }
}

bool PGModManager::canBeRunningInstance(const std::filesystem::path& instanceDir,
                                        const std::filesystem::path& pluginsFile,
                                        const std::filesystem::path& mo2Dir)
{
    std::error_code ec;
    const bool isPortableOfAnotherMO2 = std::filesystem::exists(instanceDir / L"ModOrganizer.exe", ec)
        && !std::filesystem::equivalent(instanceDir, mo2Dir, ec);
    return !isPortableOfAnotherMO2 && matchesActivePlugins(instanceDir, pluginsFile);
}

std::filesystem::path PGModManager::fileMapInstanceDir(const std::filesystem::path& configuredInstanceDir,
                                                       const std::filesystem::path& pluginsFile)
{
    const auto mo2Dir = mo2DirFromUSVFS();
    return fileMapInstanceDir(
        configuredInstanceDir, pluginsFile, mo2Dir, mo2Dir.empty() ? std::filesystem::path() : runningMO2InstanceDir());
}

std::filesystem::path PGModManager::fileMapInstanceDir(const std::filesystem::path& configuredInstanceDir,
                                                       const std::filesystem::path& pluginsFile,
                                                       const std::filesystem::path& mo2Dir,
                                                       const std::filesystem::path& runningInstanceDir)
{
    if (mo2Dir.empty())
        return { };

    std::error_code ec;
    const std::array<std::filesystem::path, 2> candidates { configuredInstanceDir, runningInstanceDir };
    for (std::size_t i = 0; i < candidates.size(); i++) {
        const auto& candidate = candidates.at(i);
        if (candidate.empty() || !isValidMO2InstanceDir(candidate))
            continue;
        if (i > 0 && std::filesystem::equivalent(candidates.front(), candidate, ec))
            continue; // Judged as the configured instance already.
        if (canBeRunningInstance(candidate, pluginsFile, mo2Dir))
            return candidate;
    }

    return { };
}

auto PGModManager::modManagerTypes() -> std::vector<ModManagerType>
{
    return { ModManagerType::None, ModManagerType::Vortex, ModManagerType::ModOrganizer2 };
}

std::string PGModManager::strFromModManagerType(const ModManagerType& type)
{
    const static auto modManagerTypeToStrMap = std::unordered_map<ModManagerType, std::string> {
        { ModManagerType::None, "None" },
        { ModManagerType::Vortex, "Vortex" },
        { ModManagerType::ModOrganizer2, "Mod Organizer 2" },
    };

    if (modManagerTypeToStrMap.contains(type))
        return modManagerTypeToStrMap.at(type);

    return modManagerTypeToStrMap.at(ModManagerType::None);
}

const std::filesystem::path& PGModManager::stagingLocation() const { return m_stagingLocation; }

auto PGModManager::modManagerTypeFromStr(const std::string& type) -> ModManagerType
{
    const static auto modManagerStrToTypeMap = std::unordered_map<std::string, ModManagerType> {
        { "None", ModManagerType::None },
        { "Vortex", ModManagerType::Vortex },
        { "Mod Organizer 2", ModManagerType::ModOrganizer2 },
    };

    if (modManagerStrToTypeMap.contains(type))
        return modManagerStrToTypeMap.at(type);

    return modManagerStrToTypeMap.at("None");
}

void PGModManager::updateStateFromModlist(bool useDefaultOrder) const
{
    // Start from the current mod snapshot.
    const auto allMods = mods();

    // Collect newly discovered mods that should be auto-enabled and assigned fresh priorities.
    std::vector<std::shared_ptr<Mod>> autoEnabledNewMods;
    autoEnabledNewMods.reserve(allMods.size());
    for (const auto& modEntry : allMods) {
        const bool hasPatchableShader
            = !modEntry->shaders.empty() && *modEntry->shaders.rbegin() > PGEnums::ShapeShader::None;
        if (!modEntry->isNew || !hasPatchableShader)
            continue;

        {
            const std::unique_lock<std::shared_mutex> modLock(modEntry->mutex);
            modEntry->isEnabled = true;
        }

        autoEnabledNewMods.push_back(modEntry);
    }

    // Sort auto-enabled new mods by shader quality/name using existing comparator behavior.
    std::ranges::stable_sort(autoEnabledNewMods,
                             [](const auto& a, const auto& b) { return PGModManager::compareMods(a, b, false); });

    // Continue priority assignment above the current maximum.
    int highestAssignedPriority = 0;
    for (const auto& modEntry : allMods)
        highestAssignedPriority = std::max(modEntry->priority, highestAssignedPriority);

    for (const auto& newModEntry : std::ranges::reverse_view(autoEnabledNewMods)) {
        const std::unique_lock<std::shared_mutex> modLock(newModEntry->mutex);
        newModEntry->priority = ++highestAssignedPriority;
    }

    const std::vector<std::shared_ptr<Mod>> modsSortedBySelectedBaseOrder
        = useDefaultOrder && m_mmType == ModManagerType::ModOrganizer2 ? modsByDefaultOrder() : modsByPriority();

    // The mod sort dialog only displays mods with shaders or meshes and assigns priorities over those
    // rows alone. Number the same subset here; including hidden mods would offset every priority by the
    // hidden-mod count and make the dialog report unsaved changes when the user changed nothing.
    std::vector<std::shared_ptr<Mod>> displayedMods;
    displayedMods.reserve(modsSortedBySelectedBaseOrder.size());
    for (const auto& modEntry : modsSortedBySelectedBaseOrder)
        if (!modEntry->shaders.empty() || modEntry->hasMeshes)
            displayedMods.push_back(modEntry);

    // Rebuild ordering to enabled-first while preserving relative order within each group.
    std::ranges::stable_partition(displayedMods, [](const auto& modEntry) { return modEntry->isEnabled; });

    const int modCount = static_cast<int>(displayedMods.size());
    for (int orderedIndex = 0; orderedIndex < modCount; ++orderedIndex) {
        const auto& modEntry = displayedMods.at(static_cast<size_t>(orderedIndex));
        if (modEntry->isEnabled) {
            const std::unique_lock<std::shared_mutex> modLock(modEntry->mutex);
            modEntry->priority = modCount - orderedIndex;
        }
    }
}

void PGModManager::addShaderToModByFile(const std::filesystem::path& relPath,
                                        const PGEnums::ShapeShader& shader) const
{
    const auto modPtr = modByFileSmart(relPath);
    if (!modPtr)
        return;

    const std::unique_lock<std::shared_mutex> modLock(modPtr->mutex);
    modPtr->shaders.insert(shader);
}

bool PGModManager::isValidMO2InstanceDir(const std::filesystem::path& instanceDir)
{
    // Check if the instance directory contains the required files. A folder that cannot be queried (a locked volume,
    // a share without access) is no instance either.
    const std::filesystem::path modOrganizerIni = instanceDir / "modorganizer.ini";

    std::error_code ec;
    return std::filesystem::exists(modOrganizerIni, ec);
}

std::string PGModManager::mo2INIRawField(const std::filesystem::path& instanceDir,
                                         const std::string& fieldName)
{
    // Find MO2 paths from ModOrganizer.ini.
    const std::filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!std::filesystem::exists(mo2IniFile))
        return { };

    std::ifstream mo2IniFileF(mo2IniFile);
    std::string mo2IniLine;
    while (getline(mo2IniFileF, mo2IniLine))
        if (mo2IniLine.starts_with(fieldName))
            return mo2IniLine.substr(fieldName.size());

    return { }; // default to empty if not found
}

std::wstring PGModManager::mo2INIField(const std::filesystem::path& instanceDir,
                                       const std::string& fieldName,
                                       const bool& isByteArray)
{
    auto fieldValue = mo2INIRawField(instanceDir, fieldName);
    if (fieldValue.empty())
        return { };

    // Remove leading and trailing quotes.
    boost::trim_if(fieldValue, boost::is_any_of("\""));

    // Remove @ByteArray( and ) from the string when isByteArray is set.
    if (isByteArray && boost::starts_with(fieldValue, mo2IniByteArrayPrefix)
        && boost::ends_with(fieldValue, mo2IniByteArraySuffix)) {
        const auto byteArrayVal
            = fieldValue.substr(strlen(mo2IniByteArrayPrefix),
                                fieldValue.size() - strlen(mo2IniByteArrayPrefix) - strlen(mo2IniByteArraySuffix));
        return decodeQtByteArrayValue(byteArrayVal);
    }

    auto parsedVal = StringUtil::utf8toUTF16(fieldValue);

    // Replace backslashes with single ones.
    boost::replace_all(parsedVal, L"\\\\", L"\\");

    return parsedVal;
}

std::filesystem::path PGModManager::gamePathFromInstanceDir(const std::filesystem::path& instanceDir)
{
    return resolveMO2GamePath(mo2INIField(instanceDir, mo2IniGameDirKey, true), instanceDir);
}

std::filesystem::path PGModManager::mo2DirFromUSVFS()
{
    // MO2 injects usvfs_x64.dll from its own install folder into every process it launches, so the folder of that
    // loaded module is the folder containing ModOrganizer.exe.
    HANDLE hSnapshot = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < moduleSnapshotMaxAttempts; ++attempt) {
        hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
        if (hSnapshot != INVALID_HANDLE_VALUE || GetLastError() != ERROR_BAD_LENGTH)
            break;
        // Transient: the module list changed while the snapshot was taken, retry.
    }

    if (hSnapshot == INVALID_HANDLE_VALUE)
        return { };

    std::filesystem::path mo2Dir;
    MODULEENTRY32W me32 { };
    me32.dwSize = sizeof(MODULEENTRY32W);
    if (Module32FirstW(hSnapshot, &me32)) {
        do { // NOLINT(cppcoreguidelines-avoid-do-while)
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
            if (boost::iequals(std::wstring(me32.szModule), std::wstring(mo2UsvfsDLLName))) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-array-to-pointer-decay)
                mo2Dir = std::filesystem::path(me32.szExePath).parent_path();
                break;
            }
        } while (Module32NextW(hSnapshot, &me32));
    }

    CloseHandle(hSnapshot);
    return mo2Dir;
}

std::filesystem::path PGModManager::findMO2Dir(const std::filesystem::path& instanceDir)
{
    // Primary source: the usvfs DLL MO2 injected into this process, exact for portable and global instances alike.
    auto mo2Dir = mo2DirFromUSVFS();
    if (!mo2Dir.empty())
        return mo2Dir;

    // Fallback: a portable instance lives in the MO2 folder itself.
    std::error_code ec;
    if (instanceDir.empty() || !std::filesystem::exists(instanceDir / mo2ExeFilename, ec))
        return { };

    mo2Dir = instanceDir;
    if (mo2Dir.is_relative()) {
        // Keep the result absolute even if the instance folder was given as a relative path.
        const auto absMO2Dir = std::filesystem::absolute(mo2Dir, ec);
        if (!ec)
            mo2Dir = absMO2Dir;
    }

    return mo2Dir;
}

std::filesystem::path PGModManager::resolveMO2GamePath(const std::filesystem::path& gamePath,
                                                       const std::filesystem::path& instanceDir)
{
    if (gamePath.empty() || gamePath.is_absolute()) {
        // Returned untouched so existing configs (and the update cache keys derived from them) keep their exact value.
        return gamePath;
    }

    // MO2 sets its working directory to the folder containing ModOrganizer.exe and uses gamePath as-is, so a relative
    // gamePath is relative to that folder.
    const auto mo2Dir = findMO2Dir(instanceDir);
    if (mo2Dir.empty())
        return gamePath;

    auto resolved = (mo2Dir / gamePath).lexically_normal();
    if (!resolved.has_filename()) {
        // Drop the trailing separator left behind by values such as "." or "Stock Game\".
        resolved = resolved.parent_path();
    }

    return resolved;
}

std::wstring PGModManager::selectedProfileFromInstanceDir(const std::filesystem::path& instanceDir)
{
    return mo2INIField(instanceDir, mo2IniProfileKey, true);
}

BethesdaGame::GameType PGModManager::gameTypeFromInstanceDir(const std::filesystem::path& instanceDir)
{
    // Get game name.
    const auto gameName = mo2INIField(instanceDir, mo2IniGameNameKey, false);

    // Get game edition.
    const auto gameEdition = mo2INIField(instanceDir, mo2IniGameEditionKey, false);

    if (gameName == L"Skyrim Special Edition") {
        if (gameEdition == L"Steam")
            return BethesdaGame::GameType::SkyrimSE;

        if (gameEdition == L"GOG")
            return BethesdaGame::GameType::SkyrimGOG;
    }

    if (gameName == L"Enderal Special Edition")
        return BethesdaGame::GameType::EnderalSE;

    if (gameName == L"Skyrim VR")
        return BethesdaGame::GameType::SkyrimVR;

    return BethesdaGame::GameType::Unknown; // default to unknown if not found
}

std::pair<std::filesystem::path,
          std::filesystem::path>
PGModManager::mo2FilePaths(const std::filesystem::path& instanceDir)
{
    // Find MO2 paths from ModOrganizer.ini.
    const std::filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!std::filesystem::exists(mo2IniFile))
        return { { }, { } };

    return { mo2FolderSetting(instanceDir, mo2IniProfilesDirKey, L"profiles"),
             mo2FolderSetting(instanceDir, mo2IniModDirKey, L"mods") };
}

std::filesystem::path PGModManager::mo2OverwriteDir(const std::filesystem::path& instanceDir)
{
    return mo2FolderSetting(instanceDir, mo2IniOverwriteDirKey, L"overwrite");
}

std::filesystem::path PGModManager::mo2FolderSetting(const std::filesystem::path& instanceDir,
                                                     const std::string& fieldName,
                                                     const wchar_t* defaultName)
{
    std::filesystem::path baseDir = mo2INIField(instanceDir, mo2IniBaseDirKey, true);
    if (baseDir.empty()) {
        // If baseDir is empty, set it to the instance directory.
        baseDir = instanceDir;
    }

    auto field = mo2INIField(instanceDir, fieldName, true);
    if (field.empty())
        return baseDir / defaultName;

    // Replace any instance of %BASE_DIR% with the base directory.
    boost::replace_all(field, StringUtil::utf8toUTF16(mo2IniBaseDirWildcard), baseDir.wstring());
    return field;
}

std::vector<std::wstring> PGModManager::mo2SkipFileSuffixes(const std::filesystem::path& instanceDir)
{
    return mo2ListSetting(instanceDir, mo2IniSkipFileSuffixesKey, { mo2DefaultSkipFileSuffix });
}

std::vector<std::wstring> PGModManager::mo2SkipDirectories(const std::filesystem::path& instanceDir)
{
    return mo2ListSetting(instanceDir, mo2IniSkipDirectoriesKey, { mo2DefaultSkipDirectory });
}

std::vector<std::wstring> PGModManager::mo2ListSetting(const std::filesystem::path& instanceDir,
                                                       const std::string& fieldName,
                                                       const std::vector<std::wstring>& defaultValue)
{
    std::wstring field = StringUtil::utf8toUTF16(mo2INIRawField(instanceDir, fieldName));
    boost::trim(field);
    if (field.empty())
        return defaultValue;
    if (field == mo2IniInvalidValue)
        return { };

    // Comma separated; an item that holds a comma, spaces or quotes is quoted, with a backslash before an escaped
    // character.
    std::vector<std::wstring> items;
    std::size_t pos = 0;
    while (pos < field.size()) {
        while (pos < field.size() && field[pos] == L' ')
            ++pos;
        if (pos >= field.size())
            break;

        std::wstring item;
        if (field[pos] == L'"') {
            ++pos;
            while (pos < field.size() && field[pos] != L'"') {
                if (field[pos] == L'\\' && pos + 1 < field.size())
                    ++pos;
                item.push_back(field[pos++]);
            }
            // The closing quote, then anything up to the next comma.
            while (pos < field.size() && field[pos] != L',')
                ++pos;
        } else {
            while (pos < field.size() && field[pos] != L',')
                item.push_back(field[pos++]);
            boost::trim(item);
        }
        if (pos < field.size())
            ++pos; // the comma

        if (!item.empty())
            items.push_back(std::move(item));
    }

    return items;
}

std::wstring PGModManager::mo2CurrentInstanceName()
{
    std::wstring value(MAX_PATH, L'\0');
    auto size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
    const auto status = RegGetValueW(HKEY_CURRENT_USER,
                                     mo2RegistryKey,
                                     mo2RegistryCurrentInstanceValue,
                                     RRF_RT_REG_SZ,
                                     nullptr,
                                     value.data(),
                                     &size);
    if (status != ERROR_SUCCESS)
        return { };

    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

std::filesystem::path PGModManager::runningMO2InstanceDir()
{
    const auto mo2Dir = mo2DirFromUSVFS();
    if (mo2Dir.empty())
        return { };

    std::filesystem::path globalInstancesDir;
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData) {
        globalInstancesDir = std::filesystem::path(localAppData) / mo2GlobalInstancesFolder;
        CoTaskMemFree(localAppData);
    }

    return runningMO2InstanceDir(mo2Dir, mo2CurrentInstanceName(), globalInstancesDir);
}

std::filesystem::path PGModManager::runningMO2InstanceDir(const std::filesystem::path& mo2Dir,
                                                          const std::wstring& currentInstanceName,
                                                          const std::filesystem::path& globalInstancesDir)
{
    // MO2's own choice at startup (InstanceManager::currentInstance): the portable instance when the lock file
    // forces it, the current global instance when one is set, the portable instance when there is one.
    std::error_code ec;
    const bool hasPortableInstance = std::filesystem::exists(mo2Dir / mo2IniFileName, ec);
    ec.clear();
    if (hasPortableInstance && std::filesystem::exists(mo2Dir / mo2PortableLockFileName, ec))
        return mo2Dir;

    if (!currentInstanceName.empty() && !globalInstancesDir.empty()) {
        const auto instanceDir = globalInstancesDir / currentInstanceName;
        if (isValidMO2InstanceDir(instanceDir))
            return instanceDir;
    }

    if (hasPortableInstance)
        return mo2Dir;

    return { };
}

bool PGModManager::compareMods(const std::shared_ptr<Mod>& a,
                               const std::shared_ptr<Mod>& b,
                               bool checkPriority)
{
    // First by priority.
    if (checkPriority && a->priority != b->priority)
        return a->priority > b->priority; // Higher priority first
    // Then by mod manager order (MO2 only).
    if (a->modManagerOrder != b->modManagerOrder)
        return a->modManagerOrder < b->modManagerOrder; // Lower modManagerOrder first
    // Then by shader.
    const auto maxElemAIt = std::ranges::max_element(a->shaders);
    const auto maxElemBIt = std::ranges::max_element(b->shaders);
    if (maxElemAIt != a->shaders.end() && maxElemBIt != b->shaders.end()) {
        if (*maxElemAIt != *maxElemBIt)
            return *maxElemAIt > *maxElemBIt;
    } else if (maxElemAIt != a->shaders.end()) {
        return true;
    } else if (maxElemBIt != b->shaders.end()) {
        return false;
    }
    return a->name < b->name; // Alphabetical order as last resort
}
