#include "PGModManager.hpp"

#include "PGGlobals.hpp"
#include "PGRunCache.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
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
#include <cctype>
#include <cstdint>
#include <cstring>
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
    if (m_modFileMap.contains(relPath))
        return m_modFileMap.at(relPath);

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
    PGRunCache::recordModOfFile(relPath, !mod ? std::wstring() : mod->name);

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

    const auto deploymentFile = deploymentDir / "vortex.deployment.json";

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
    if (!std::filesystem::exists(modListFile)) {
        throw std::runtime_error("Mod Organizer 2 modlist.txt file does not exist: "
                                 + StringUtil::utf16toUTF8(modListFile.wstring()));
    }

    std::ifstream modListFileF(modListFile);
    bool foundOneMod = false;

    // Loop through modlist.txt.
    std::string modStr;
    int basePriority = 0;
    std::unordered_set<std::wstring> foundMods;
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

        // Loop through all files in mod.
        mod.erase(0, 1); // remove +
        const auto curModDir = modDir / mod;

        // Check if mod folder exists.
        if (!std::filesystem::exists(curModDir)) {
            Logger::debug(L"Mod directory from modlist.txt does not exist: {}", curModDir.wstring());
            Logger::critical("modlist.txt from MO2 does not reflect the contents of the mods folder. This should not "
                             "happen unless MO2 is in a corrupt state.");
            return;
        }

        // Check if mod dir is output dir.
        if (std::filesystem::equivalent(curModDir, outputDir)) {
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
                for (auto it = std::filesystem::recursive_directory_iterator(
                         curSearchDir, std::filesystem::directory_options::skip_permission_denied);
                     it != std::filesystem::recursive_directory_iterator();
                     ++it) {
                    const auto file = *it;

                    if (BethesdaDirectory::isHidden(file.path())) {
                        if (file.is_directory()) {
                            // If it's a directory, don't recurse into it.
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    if (!std::filesystem::is_regular_file(file))
                        continue;

                    // Skip meta.ini file.
                    if (boost::iequals(file.path().filename().wstring(), L"meta.ini"))
                        continue;

                    const auto relPath = std::filesystem::relative(file, curModDir);
                    const std::filesystem::path relPathLower = StringUtil::toLowerASCII(relPath.wstring());
                    // Check if already in map.
                    if (m_modFileMap.contains(relPathLower))
                        continue;

                    Logger::trace(L"Mapping file to mod: {} -> {}", relPathLower.wstring(), mod);

                    m_modFileMap[relPathLower] = modPtr;
                }
            } catch (const std::filesystem::filesystem_error& e) {
                Logger::error(L"Error reading mod directory {}: {}", mod, StringUtil::asciitoUTF16(e.what()));
            }
        }

        // Map any BSAs.
        for (const auto& file : std::filesystem::directory_iterator(curModDir)) {
            if (file.is_regular_file() && boost::iequals(file.path().extension().wstring(), ".bsa")) {
                const auto relPath = std::filesystem::relative(file, curModDir);
                const std::filesystem::path relPathLower = StringUtil::toLowerASCII(relPath.wstring());
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

    modListFileF.close();
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

    // The mod sort dialog only displays mods with shaders or meshes and assigns priorities over those.
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
    // Check if the instance directory contains the required files.
    const std::filesystem::path modOrganizerIni = instanceDir / "modorganizer.ini";

    return std::filesystem::exists(modOrganizerIni);
}

std::wstring PGModManager::mo2INIField(const std::filesystem::path& instanceDir,
                                       const std::string& fieldName,
                                       const bool& isByteArray)
{
    // Find MO2 paths from ModOrganizer.ini.
    const std::filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!std::filesystem::exists(mo2IniFile))
        return { };

    std::ifstream mo2IniFileF(mo2IniFile);
    std::string mo2IniLine;
    while (getline(mo2IniFileF, mo2IniLine)) {
        if (mo2IniLine.starts_with(fieldName)) {
            auto fieldValue = mo2IniLine.substr(fieldName.size());
            mo2IniFileF.close();

            // Remove leading and trailing quotes.
            boost::trim_if(fieldValue, boost::is_any_of("\""));

            // remove @ByteArray( and ) from the string if isByteArray is true
            if (isByteArray && boost::starts_with(fieldValue, mo2IniByteArrayPrefix)
                && boost::ends_with(fieldValue, mo2IniByteArraySuffix)) {
                const auto byteArrayVal = fieldValue.substr(strlen(mo2IniByteArrayPrefix),
                                                            fieldValue.size() - strlen(mo2IniByteArrayPrefix)
                                                                - strlen(mo2IniByteArraySuffix));
                return decodeQtByteArrayValue(byteArrayVal);
            }

            auto parsedVal = StringUtil::utf8toUTF16(fieldValue);

            // Replace backslashes with single ones.
            boost::replace_all(parsedVal, L"\\\\", L"\\");

            return parsedVal;
        }
    }

    mo2IniFileF.close();

    return { }; // default to empty if not found
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

    // MO2 sets its working directory to the folder containing ModOrganizer.exe and uses gamePath as-is, so a relative.
    // GamePath is relative to that folder.
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

    auto profileDirField = mo2INIField(instanceDir, mo2IniProfilesDirKey, true);
    auto modDirField = mo2INIField(instanceDir, mo2IniModDirKey, true);
    std::filesystem::path baseDir = mo2INIField(instanceDir, mo2IniBaseDirKey, true);

    if (baseDir.empty()) {
        // If baseDir is empty, set it to the instance directory.
        baseDir = instanceDir;
    }

    // Replace any instance of %BASE_DIR% with the base directory.
    const auto baseDirWildcardW = StringUtil::utf8toUTF16(mo2IniBaseDirWildcard);
    boost::replace_all(profileDirField, baseDirWildcardW, baseDir.wstring());
    boost::replace_all(modDirField, baseDirWildcardW, baseDir.wstring());

    std::filesystem::path profileDir = profileDirField;
    std::filesystem::path modDir = modDirField;

    if (profileDir.empty())
        profileDir = baseDir / "profiles";

    if (modDir.empty())
        modDir = baseDir / "mods";

    return { profileDir, modDir };
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
