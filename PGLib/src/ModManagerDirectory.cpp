#include "ModManagerDirectory.hpp"

#include <boost/algorithm/string.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <fstream>

#include <regex>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include "BethesdaDirectory.hpp"
#include "ParallaxGenUtil.hpp"

using namespace std;

ModManagerDirectory::ModManagerDirectory(const ModManagerType& mmType)
    : m_mmType(mmType)
{
}

auto ModManagerDirectory::getModFileMap() const -> const unordered_map<filesystem::path, shared_ptr<Mod>>&
{
    return m_modFileMap;
}

auto ModManagerDirectory::getModByFile(const filesystem::path& relPath) const -> shared_ptr<Mod>
{
    auto relPathLower = filesystem::path(ParallaxGenUtil::toLowerASCII(relPath.wstring()));
    if (m_modFileMap.contains(relPathLower)) {
        return m_modFileMap.at(relPathLower);
    }

    return nullptr;
}

auto ModManagerDirectory::getMods() const -> vector<shared_ptr<Mod>>
{
    vector<shared_ptr<Mod>> mods;
    for (const auto& [modName, mod] : m_modMap) {
        if (modName.empty()) {
            continue; // skip empty mod
        }

        mods.push_back(mod);
    }

    return mods;
}

auto ModManagerDirectory::getMod(const wstring& modName) const -> shared_ptr<Mod>
{
    if (m_modMap.contains(modName)) {
        return m_modMap.at(modName);
    }

    return nullptr;
}

void ModManagerDirectory::loadJSON(const nlohmann::json& json)
{
    if (!json.is_object()) {
        throw runtime_error("JSON is not an object");
    }

    for (const auto& [modName, priority] : json.items()) {
        if (modName.empty()) {
            continue;
        }

        if (!priority.is_number_integer()) {
            throw runtime_error("JSON mod priority is not an integer");
        }

        shared_ptr<Mod> modPtr = nullptr;
        const auto modNameWStr = ParallaxGenUtil::utf8toUTF16(modName);
        if (!m_modMap.contains(modNameWStr)) {
            // create mod if it doesn't exist
            modPtr = make_shared<Mod>();
            modPtr->name = modNameWStr;
            m_modMap[modNameWStr] = modPtr;
        }

        modPtr->isNew = false;
        modPtr->priority = priority.get<int>();
    }
}

auto ModManagerDirectory::getJSON() -> nlohmann::json
{
    nlohmann::json json = nlohmann::json::object();

    for (const auto& [modName, mod] : m_modMap) {
        if (mod->priority != -1) {
            json[ParallaxGenUtil::utf16toUTF8(modName)] = mod->priority;
        }
    }

    return json;
}

void ModManagerDirectory::populateModFileMapVortex(const filesystem::path& deploymentDir)
{
    // required file is vortex.deployment.json in the data folder
    spdlog::info("Populating mods from Vortex");

    const auto deploymentFile = deploymentDir / "vortex.deployment.json";

    if (!filesystem::exists(deploymentFile)) {
        throw runtime_error(
            "Vortex deployment file does not exist: " + ParallaxGenUtil::utf16toUTF8(deploymentFile.wstring()));
    }

    ifstream vortexDepFileF(deploymentFile);
    nlohmann::json vortexDeployment = nlohmann::json::parse(vortexDepFileF);
    vortexDepFileF.close();

    // Check that files field exists
    if (!vortexDeployment.contains("files")) {
        throw runtime_error("Vortex deployment file does not contain 'files' field: "
            + ParallaxGenUtil::utf16toUTF8(deploymentFile.wstring()));
    }

    // loop through files
    for (const auto& file : vortexDeployment["files"]) {
        auto relPath = filesystem::path(ParallaxGenUtil::utf8toUTF16(file["relPath"].get<string>()));

        // Check if relPath is within s_foldersToMap
        // Get the first path component of relPath
        if (!boost::iequals(relPath.extension().wstring(), ".bsa")
            && !s_foldersToMap.contains(boost::to_lower_copy(relPath.begin()->wstring()))) {
            // skip if not mapping from this folder
            continue;
        }

        auto modName = ParallaxGenUtil::utf8toUTF16(file["source"].get<string>());

        // filter out modname suffix
        const static wregex vortexSuffixRe(L"-[0-9]+-.*");
        modName = regex_replace(modName, vortexSuffixRe, L"");

        shared_ptr<Mod> modPtr = nullptr;
        if (m_modMap.contains(modName)) {
            // skip if already in map
            modPtr = m_modMap.at(modName);
        } else {
            modPtr = make_shared<Mod>();
            modPtr->name = modName;
            modPtr->isNew = true;
            modPtr->priority = -1;
        }

        modPtr->modManagerOrder = 0; // Vortex does not have a mod manager order system by default

        // Update file map
        spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPath.wstring(), modName);

        m_modFileMap[ParallaxGenUtil::toLowerASCII(relPath.wstring())] = modPtr;
    }
}

void ModManagerDirectory::populateModFileMapMO2(const filesystem::path& instanceDir, const wstring& profile,
    const filesystem::path& outputDir, const bool& useMO2Order)
{
    // required file is modlist.txt in the profile folder

    spdlog::info("Populating mods from Mod Organizer 2");

    // First read modorganizer.ini in the instance folder to get the profiles and mods folders
    const filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!filesystem::exists(mo2IniFile)) {
        throw runtime_error(
            "Mod Organizer 2 ini file does not exist: " + ParallaxGenUtil::utf16toUTF8(mo2IniFile.wstring()));
    }

    auto mo2Paths = getMO2FilePaths(instanceDir);
    const auto profileDir = mo2Paths.first;
    const auto modDir = mo2Paths.second;

    // Find location of modlist.txt
    const auto modListFile = profileDir / profile / "modlist.txt";
    if (!filesystem::exists(modListFile)) {
        throw runtime_error(
            "Mod Organizer 2 modlist.txt file does not exist: " + ParallaxGenUtil::utf16toUTF8(modListFile.wstring()));
    }

    ifstream modListFileF(modListFile);
    bool foundOneMod = false;

    // loop through modlist.txt
    string modStr;
    int basePriority = 0;
    while (getline(modListFileF, modStr)) {
        wstring mod = ParallaxGenUtil::utf8toUTF16(modStr);
        if (mod.empty()) {
            // skip empty lines
            continue;
        }

        if (mod.starts_with(L"-") || mod.starts_with(L"*")) {
            // Skip disabled and uncontrolled mods
            continue;
        }

        if (mod.starts_with(L"#")) {
            // Skip comments
            continue;
        }

        if (mod.ends_with(L"_separator")) {
            // Skip separators
            continue;
        }

        // loop through all files in mod
        mod.erase(0, 1); // remove +
        const auto curModDir = modDir / mod;

        // Check if mod folder exists
        if (!filesystem::exists(curModDir)) {
            spdlog::warn(L"Mod directory from modlist.txt does not exist: {}", curModDir.wstring());
            continue;
        }

        // check if mod dir is output dir
        if (filesystem::equivalent(curModDir, outputDir)) {
            spdlog::critical(
                L"If outputting to MO2 you must disable the mod {} first to prevent issues with MO2 VFS", mod);
            exit(1);
        }

        foundOneMod = true;

        shared_ptr<Mod> modPtr = nullptr;
        if (m_modMap.contains(mod)) {
            // skip if already in map
            modPtr = m_modMap.at(mod);
        } else {
            modPtr = make_shared<Mod>();
            modPtr->name = mod;
            modPtr->isNew = true;
            modPtr->priority = -1;
        }

        modPtr->modManagerOrder = basePriority++;
        if (useMO2Order) {
            modPtr->priority = modPtr->modManagerOrder;
        }

        m_modMap[mod] = modPtr;

        // loop through each folder to map
        for (const auto& folder : s_foldersToMap) {
            const auto curSearchDir = curModDir / folder;
            if (!filesystem::exists(curSearchDir)) {
                // skip if folder doesn't exist
                continue;
            }

            try {
                for (auto it = filesystem::recursive_directory_iterator(
                         curSearchDir, filesystem::directory_options::skip_permission_denied);
                    it != filesystem::recursive_directory_iterator(); ++it) {
                    const auto file = *it;

                    if (BethesdaDirectory::isHidden(file.path())) {
                        if (file.is_directory()) {
                            // If it's a directory, don't recurse into it
                            it.disable_recursion_pending();
                        }
                        continue;
                    }

                    if (!filesystem::is_regular_file(file)) {
                        continue;
                    }

                    // skip meta.ini file
                    if (boost::iequals(file.path().filename().wstring(), L"meta.ini")) {
                        continue;
                    }

                    auto relPath = filesystem::relative(file, curModDir);
                    const filesystem::path relPathLower = ParallaxGenUtil::toLowerASCII(relPath.wstring());
                    // check if already in map
                    if (m_modFileMap.contains(relPathLower)) {
                        continue;
                    }

                    spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPathLower.wstring(), mod);

                    m_modFileMap[relPathLower] = modPtr;
                }
            } catch (const filesystem::filesystem_error& e) {
                spdlog::error(
                    L"Error reading mod directory {} (skipping): {}", mod, ParallaxGenUtil::asciitoUTF16(e.what()));
            }
        }

        // map any BSAs
        for (const auto& file : filesystem::directory_iterator(curModDir)) {
            if (file.is_regular_file() && boost::iequals(file.path().extension().wstring(), ".bsa")) {
                const auto relPath = filesystem::relative(file, curModDir);
                const filesystem::path relPathLower = ParallaxGenUtil::toLowerASCII(relPath.wstring());
                // check if already in map
                if (m_modFileMap.contains(relPathLower)) {
                    continue;
                }

                spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPathLower.wstring(), mod);

                m_modFileMap[relPathLower] = modPtr;
            }
        }
    }

    if (!foundOneMod) {
        spdlog::critical(L"MO2 modlist.txt was empty, no mods found");
        exit(1);
    }

    if (useMO2Order) {
        // invert priorities
        for (const auto& [modName, mod] : m_modMap) {
            if (mod->priority != -1) {
                mod->priority = basePriority - mod->priority - 1;
            }
        }
    }

    modListFileF.close();
}

auto ModManagerDirectory::getModManagerTypes() -> vector<ModManagerType>
{
    return { ModManagerType::NONE, ModManagerType::VORTEX, ModManagerType::MODORGANIZER2 };
}

auto ModManagerDirectory::getStrFromModManagerType(const ModManagerType& type) -> string
{
    const static auto modManagerTypeToStrMap = unordered_map<ModManagerType, string> { { ModManagerType::NONE, "None" },
        { ModManagerType::VORTEX, "Vortex" }, { ModManagerType::MODORGANIZER2, "Mod Organizer 2" } };

    if (modManagerTypeToStrMap.contains(type)) {
        return modManagerTypeToStrMap.at(type);
    }

    return modManagerTypeToStrMap.at(ModManagerType::NONE);
}

auto ModManagerDirectory::getModManagerTypeFromStr(const string& type) -> ModManagerType
{
    const static auto modManagerStrToTypeMap = unordered_map<string, ModManagerType> { { "None", ModManagerType::NONE },
        { "Vortex", ModManagerType::VORTEX }, { "Mod Organizer 2", ModManagerType::MODORGANIZER2 } };

    if (modManagerStrToTypeMap.contains(type)) {
        return modManagerStrToTypeMap.at(type);
    }

    return modManagerStrToTypeMap.at("None");
}

auto ModManagerDirectory::getMO2ProfilesFromInstanceDir(const filesystem::path& instanceDir) -> vector<wstring>
{
    const auto profileDir = getMO2FilePaths(instanceDir).first;
    if (profileDir.empty()) {
        return {};
    }

    // check if the "profiles" folder exists
    if (!filesystem::exists(profileDir)) {
        // set instance directory text to red
        return {};
    }

    // Find all directories within "profiles"
    vector<wstring> profiles;
    for (const auto& entry : filesystem::directory_iterator(profileDir)) {
        if (entry.is_directory()) {
            profiles.push_back(entry.path().filename().wstring());
        }
    }

    return profiles;
}

auto ModManagerDirectory::getMO2FilePaths(const std::filesystem::path& instanceDir)
    -> std::pair<std::filesystem::path, std::filesystem::path>
{
    // Find MO2 paths from ModOrganizer.ini
    wstring profileDirField;
    wstring modDirField;
    filesystem::path baseDir;

    const filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!filesystem::exists(mo2IniFile)) {
        return { {}, {} };
    }

    ifstream mo2IniFileF(mo2IniFile);
    string mo2IniLine;
    while (getline(mo2IniFileF, mo2IniLine)) {
        const wstring mo2IniLineWstr = ParallaxGenUtil::utf8toUTF16(mo2IniLine);

        if (mo2IniLine.starts_with(MO2INI_PROFILESDIR_KEY)) {
            profileDirField = mo2IniLineWstr.substr(strlen(MO2INI_PROFILESDIR_KEY));
        } else if (mo2IniLine.starts_with(MO2INI_MODDIR_KEY)) {
            modDirField = mo2IniLineWstr.substr(strlen(MO2INI_MODDIR_KEY));
        } else if (mo2IniLine.starts_with(MO2INI_BASEDIR_KEY)) {
            baseDir = mo2IniLineWstr.substr(strlen(MO2INI_BASEDIR_KEY));
        }
    }

    mo2IniFileF.close();

    // replace any instance of %BASE_DIR% with the base directory
    boost::replace_all(profileDirField, MO2INI_BASEDIR_WILDCARD, baseDir.wstring());
    boost::replace_all(modDirField, MO2INI_BASEDIR_WILDCARD, baseDir.wstring());

    filesystem::path profileDir = profileDirField;
    filesystem::path modDir = modDirField;

    if (profileDir.empty()) {
        if (baseDir.empty()) {
            profileDir = instanceDir / "profiles";
        } else {
            profileDir = baseDir / "profiles";
        }
    }

    if (modDir.empty()) {
        if (baseDir.empty()) {
            modDir = instanceDir / "mods";
        } else {
            modDir = baseDir / "mods";
        }
    }

    return { profileDir, modDir };
}
