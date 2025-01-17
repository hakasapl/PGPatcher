#include "ModManagerDirectory.hpp"

#include <boost/algorithm/string.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <fstream>
#include <regex>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include "ParallaxGenUtil.hpp"

using namespace std;

ModManagerDirectory::ModManagerDirectory(const ModManagerType& mmType)
    : m_mmType(mmType)
{
}

auto ModManagerDirectory::getModFileMap() const -> const unordered_map<filesystem::path, wstring>&
{
    return m_modFileMap;
}

auto ModManagerDirectory::getMod(const filesystem::path& relPath) const -> wstring
{
    auto relPathLower = filesystem::path(ParallaxGenUtil::toLowerASCII(relPath.wstring()));
    if (m_modFileMap.contains(relPathLower)) {
        return m_modFileMap.at(relPathLower);
    }

    return L"";
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
        auto modName = ParallaxGenUtil::utf8toUTF16(file["source"].get<string>());

        // filter out modname suffix
        const static wregex vortexSuffixRe(L"-[0-9]+-.*");
        modName = regex_replace(modName, vortexSuffixRe, L"");

        m_allMods.insert(modName);

        // Update file map
        spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPath.wstring(), modName);

        if (!ParallaxGenUtil::containsOnlyAscii(relPath.wstring())) {
            spdlog::debug(L"Path {} from {} contains non-ASCII characters", relPath.wstring(), deploymentDir.wstring());
        }

        m_modFileMap[ParallaxGenUtil::toLowerASCII(relPath.wstring())] = modName;
    }
}

void ModManagerDirectory::populateModFileMapMO2(
    const filesystem::path& instanceDir, const wstring& profile, const filesystem::path& outputDir)
{
    // required file is modlist.txt in the profile folder

    spdlog::info("Populating mods from Mod Organizer 2");

    // First read modorganizer.ini in the instance folder to get the profiles and mods folders
    const filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!filesystem::exists(mo2IniFile)) {
        throw runtime_error(
            "Mod Organizer 2 ini file does not exist: " + ParallaxGenUtil::utf16toUTF8(mo2IniFile.wstring()));
    }

    // Find MO2 paths from ModOrganizer.ini
    filesystem::path profileDir;
    filesystem::path modDir;
    filesystem::path baseDir;

    ifstream mo2IniFileF(mo2IniFile);
    string mo2IniLine;
    while (getline(mo2IniFileF, mo2IniLine)) {
        if (mo2IniLine.starts_with(MO2INI_PROFILESDIR_KEY)) {
            profileDir = instanceDir / mo2IniLine.substr(strlen(MO2INI_PROFILESDIR_KEY));
        } else if (mo2IniLine.starts_with(MO2INI_MODDIR_KEY)) {
            modDir = instanceDir / mo2IniLine.substr(strlen(MO2INI_MODDIR_KEY));
        } else if (mo2IniLine.starts_with(MO2INI_BASEDIR_KEY)) {
            baseDir = instanceDir / mo2IniLine.substr(strlen(MO2INI_BASEDIR_KEY));
        }
    }

    mo2IniFileF.close();

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

    // Find location of modlist.txt
    const auto modListFile = profileDir / profile / "modlist.txt";
    if (!filesystem::exists(modListFile)) {
        throw runtime_error(
            "Mod Organizer 2 modlist.txt file does not exist: " + ParallaxGenUtil::utf16toUTF8(modListFile.wstring()));
    }

    ifstream modListFileF(modListFile);

    // loop through modlist.txt
    string modStr;
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

        m_allMods.insert(mod);
        m_inferredOrder.insert(m_inferredOrder.begin(), mod);

        try {
            for (const auto& file : filesystem::recursive_directory_iterator(
                     curModDir, filesystem::directory_options::skip_permission_denied)) {
                if (!filesystem::is_regular_file(file)) {
                    continue;
                }

                // skip meta.ini file
                if (boost::iequals(file.path().filename().wstring(), L"meta.ini")) {
                    continue;
                }

                auto relPath = filesystem::relative(file, curModDir);
                spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPath.wstring(), mod);

                if (!ParallaxGenUtil::containsOnlyAscii(relPath.wstring())) {
                    spdlog::debug(
                        L"Path {} in directory {} contains non-ASCII characters", relPath.wstring(), modDir.wstring());
                }

                const filesystem::path relPathLower = ParallaxGenUtil::toLowerASCII(relPath.wstring());
                // check if already in map
                if (m_modFileMap.contains(relPathLower)) {
                    continue;
                }

                m_modFileMap[relPathLower] = mod;
            }
        } catch (const filesystem::filesystem_error& e) {
            spdlog::error(
                L"Error reading mod directory {} (skipping): {}", mod, ParallaxGenUtil::asciitoUTF16(e.what()));
        }
    }

    modListFileF.close();
}

auto ModManagerDirectory::getModManagerTypes() -> vector<ModManagerType>
{
    return { ModManagerType::None, ModManagerType::Vortex, ModManagerType::ModOrganizer2 };
}

auto ModManagerDirectory::getStrFromModManagerType(const ModManagerType& type) -> string
{
    const static auto modManagerTypeToStrMap = unordered_map<ModManagerType, string> { { ModManagerType::None, "None" },
        { ModManagerType::Vortex, "Vortex" }, { ModManagerType::ModOrganizer2, "Mod Organizer 2" } };

    if (modManagerTypeToStrMap.contains(type)) {
        return modManagerTypeToStrMap.at(type);
    }

    return modManagerTypeToStrMap.at(ModManagerType::None);
}

auto ModManagerDirectory::getModManagerTypeFromStr(const string& type) -> ModManagerType
{
    const static auto modManagerStrToTypeMap = unordered_map<string, ModManagerType> { { "None", ModManagerType::None },
        { "Vortex", ModManagerType::Vortex }, { "Mod Organizer 2", ModManagerType::ModOrganizer2 } };

    if (modManagerStrToTypeMap.contains(type)) {
        return modManagerStrToTypeMap.at(type);
    }

    return modManagerStrToTypeMap.at("None");
}

auto ModManagerDirectory::getMO2ProfilesFromInstanceDir(const filesystem::path& instanceDir) -> vector<wstring>
{
    // First read modorganizer.ini in the instance folder to get the profiles and mods folders
    const filesystem::path mo2IniFile = instanceDir / L"modorganizer.ini";
    if (!filesystem::exists(mo2IniFile)) {
        return {};
    }

    filesystem::path profileDir;
    filesystem::path baseDir;

    ifstream mo2IniFileF(mo2IniFile);
    string mo2IniLine;
    while (getline(mo2IniFileF, mo2IniLine)) {
        if (mo2IniLine.starts_with(MO2INI_PROFILESDIR_KEY)) {
            profileDir = instanceDir / mo2IniLine.substr(strlen(MO2INI_PROFILESDIR_KEY));
        } else if (mo2IniLine.starts_with(MO2INI_BASEDIR_KEY)) {
            baseDir = instanceDir / mo2IniLine.substr(strlen(MO2INI_BASEDIR_KEY));
        }
    }

    mo2IniFileF.close();

    if (profileDir.empty()) {
        if (baseDir.empty()) {
            profileDir = instanceDir / "profiles";
        } else {
            profileDir = baseDir / "profiles";
        }
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

auto ModManagerDirectory::getInferredOrder() const -> const vector<wstring>& { return m_inferredOrder; }
