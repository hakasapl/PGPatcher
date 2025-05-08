#include "ModManagerDirectory.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/detail/adjacency_list.hpp>
#include <boost/graph/topological_sort.hpp>

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/split.hpp>
#include <fstream>

#include <memory>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include <spdlog/spdlog.h>

#include "BethesdaGame.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenUtil.hpp"

using namespace std;

ModManagerDirectory::ModManagerDirectory(const BethesdaGame& bg, const ModManagerType& mmType)
    : m_mmType(mmType)
    , m_bg(bg)
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

    for (const auto& [modSet, winningMod] : json.items()) {
        if (modSet.empty()) {
            continue;
        }

        // merge modSet split by ',' into an unordered_set
        unordered_set<wstring> modSetSplitStr;
        boost::split(modSetSplitStr, modSet, boost::is_any_of(","), boost::token_compress_on);

        unordered_set<shared_ptr<Mod>, Mod::ModHash> modSetSplit;
        for (const auto& modName : modSetSplitStr) {
            const auto& modPtr = createOrGetNewModPtr(modName);
            modSetSplit.insert(modPtr);
        }

        // add to conflict resolution map
        const lock_guard<mutex> lock(m_modConflictResolutionMutex);
        m_modConflictResolution[modSetSplit] = createOrGetNewModPtr(winningMod);
    }
}

auto ModManagerDirectory::getJSON() -> nlohmann::json
{
    nlohmann::json json = nlohmann::json::object();

    const lock_guard<mutex> lock(m_modConflictResolutionMutex);
    for (const auto& [modSet, winningMod] : m_modConflictResolution) {
        // convert modSet to string
        string modSetStr;
        for (const auto& mod : modSet) {
            if (!modSetStr.empty()) {
                modSetStr += ",";
            }
            modSetStr += ParallaxGenUtil::utf16toUTF8(mod->name);
        }

        // add to json
        json[modSetStr] = ParallaxGenUtil::utf16toUTF8(winningMod->name);
    }

    return json;
}

// Structs to help build the dependency graph for vortex
struct VertexProperties {
    std::string modName;
};
using Graph = boost::adjacency_list<boost::setS, boost::vecS, boost::directedS, VertexProperties>;
using Vertex = boost::graph_traits<Graph>::vertex_descriptor;

void ModManagerDirectory::populateModFileMapVortex(const filesystem::path& deploymentDir)
{
    // required file is vortex.deployment.json in the data folder
    spdlog::info("Populating mods from Vortex");

    unordered_set<string> vortexModNames;
    unordered_map<string, string> vortexAttrResults;
    string lookupKey;

    {
        // get vortex mod attributes
        if (ParallaxGenUtil::isProcessRunning(L"Vortex.exe")) {
            spdlog::critical("Vortex is running, please close it before running PGPatcher");
            exit(1);
        }

        lookupKey = "persistent.mods.";
        if (m_bg.getGameType() == BethesdaGame::GameType::SKYRIM) {
            lookupKey += "skyrim";
        } else if (m_bg.getGameType() == BethesdaGame::GameType::SKYRIM_SE
            || m_bg.getGameType() == BethesdaGame::GameType::SKYRIM_GOG) {
            lookupKey += "skyrimse";
        } else if (m_bg.getGameType() == BethesdaGame::GameType::SKYRIM_VR) {
            lookupKey += "skyrimvr";
        } else if (m_bg.getGameType() == BethesdaGame::GameType::ENDERAL) {
            lookupKey += "enderal";
        } else if (m_bg.getGameType() == BethesdaGame::GameType::ENDERAL_SE) {
            lookupKey += "enderalse";
        } else {
            throw runtime_error("Game type not supported for vortex lookup");
        }

        const auto vortexExePath = ParallaxGenUtil::utf16toUTF8((getVortexPath() / "Vortex.exe").wstring());

        const string cmd = R"(")" + vortexExePath + R"(" --get ")" + lookupKey + R"(")";
        const auto cmdOut = ParallaxGenUtil::execCommand(cmd);

        // first, build an unordered_set of vortex mod names
        for (const auto& line : cmdOut) {
            const auto equalsPos = line.find('=');
            if (equalsPos == string::npos) {
                // no equal in line, we can skip
                continue;
            }

            // get string before equal
            const auto param = line.substr(0, equalsPos - 1);
            auto value = line.substr(equalsPos + 2);
            // remove wrapped quotes if they exist
            if (value.starts_with('"') && value.ends_with('"')) {
                value = value.substr(1, value.size() - 2);
            }

            if (param.ends_with(".attributes.name")) {
                // get value (after equal)
                vortexModNames.insert(value);
            }

            // add to vortexAttrResults
            vortexAttrResults[param] = value;
        }
    }

    // loop through each mod name to find friendly names
    unordered_map<string, wstring> vortexModNameMap;

    {
        Graph vortexDepGraph;
        std::unordered_map<std::string, Vertex> nameToVertex;
        unordered_map<string, unordered_set<string>> tempLooseFileConflictTracker;
        for (const auto& modName : vortexModNames) {
            auto modNameLookup = lookupKey + ".";
            modNameLookup.append(modName);

            // check if rules exist
            const string modRulesParam = modNameLookup + ".rules";
            if (vortexAttrResults.contains(modRulesParam)) {
                // convert value to json
                const auto rules = nlohmann::json::parse(vortexAttrResults.at(modRulesParam));
                if (rules.is_array()) {
                    for (const auto& rule : rules) {
                        if (!rule.contains("reference") || !rule["reference"].contains("id")) {
                            continue;
                        }

                        if (rule["type"] != "before" && rule["type"] != "after") {
                            continue;
                        }

                        // get mod ID of corresponding mod
                        const auto otherModName = rule["reference"]["id"].get<string>();

                        if (!nameToVertex.contains(modName)) {
                            const Vertex v = boost::add_vertex(vortexDepGraph);
                            vortexDepGraph[v].modName = modName;
                            nameToVertex[modName] = v;
                        }
                        if (!nameToVertex.contains(otherModName)) {
                            const Vertex v = boost::add_vertex(vortexDepGraph);
                            vortexDepGraph[v].modName = otherModName;
                            nameToVertex[otherModName] = v;
                        }

                        if (rule["type"] == "before") {
                            boost::add_edge(nameToVertex[otherModName], nameToVertex[modName], vortexDepGraph);
                        } else if (rule["type"] == "after") {
                            boost::add_edge(nameToVertex[modName], nameToVertex[otherModName], vortexDepGraph);
                        }

                        tempLooseFileConflictTracker[modName].insert(otherModName);
                    }
                }
            }

            // find mod friendly name (in order of preference)
            const string modCustomNameParam = modNameLookup + ".attributes.customFileName";
            if (vortexAttrResults.contains(modCustomNameParam)) {
                vortexModNameMap[modName] = ParallaxGenUtil::utf8toUTF16(vortexAttrResults.at(modCustomNameParam));
                continue;
            }

            const string modLogicalNameParam = modNameLookup + ".attributes.logicalFileName";
            if (vortexAttrResults.contains(modLogicalNameParam)) {
                vortexModNameMap[modName] = ParallaxGenUtil::utf8toUTF16(vortexAttrResults.at(modLogicalNameParam));
                continue;
            }

            const string modNameParam = modNameLookup + ".attributes.name";
            if (vortexAttrResults.contains(modNameParam)) {
                vortexModNameMap[modName] = ParallaxGenUtil::utf8toUTF16(vortexAttrResults.at(modNameParam));
                continue;
            }
        }

        // Sort graph
        vector<Vertex> topoOrder;
        try {
            topological_sort(vortexDepGraph, back_inserter(topoOrder));
        } catch (...) {
            spdlog::error(
                "You have cyclical dependencies in your vortex mods, please fix them before running PGPatcher");
            exit(1);
        }

        for (int i = 0; i < topoOrder.size(); ++i) {
            const auto& curModFriendlyName = vortexModNameMap.at(vortexDepGraph[topoOrder[i]].modName);
            auto curModPtr = createOrGetNewModPtr(curModFriendlyName);
            curModPtr->modManagerOrder = i;
        }

        // fix naming of m_looseFileConflictTracker
        for (auto& [curMod, conflictMods] : tempLooseFileConflictTracker) {
            const auto& curModFriendlyName = vortexModNameMap.at(curMod);
            const auto& curModPtr = createOrGetNewModPtr(curModFriendlyName);

            const lock_guard<mutex> lock(m_modLooseFileConflictsMutex);

            for (const auto& conflictMod : conflictMods) {
                const auto& otherModFriendlyName = vortexModNameMap.at(conflictMod);
                const auto& otherModPtr = createOrGetNewModPtr(otherModFriendlyName);

                m_modLooseFileConflicts[curModPtr].insert(otherModPtr);
                m_modLooseFileConflicts[otherModPtr].insert(curModPtr);
            }
        }
    }

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
    unordered_set<wstring> foundMods;
    for (const auto& file : vortexDeployment["files"]) {
        auto relPath = filesystem::path(ParallaxGenUtil::utf8toUTF16(file["relPath"].get<string>()));

        // Check if relPath is within s_foldersToMap
        // Get the first path component of relPath
        if (!PGGlobals::s_foldersToMap.contains(boost::to_lower_copy(relPath.begin()->wstring()))) {
            // skip if not mapping from this folder
            continue;
        }

        const auto modName = file["source"].get<string>();
        const wstring& modFriendlyName = vortexModNameMap.at(modName);

        auto modPtr = createOrGetNewModPtr(modFriendlyName);

        // Update file map
        spdlog::trace(L"ModManagerDirectory | Adding Files to Map : {} -> {}", relPath.wstring(), modFriendlyName);

        m_modMap[modFriendlyName] = modPtr;
        foundMods.insert(modFriendlyName);
        m_modFileMap[ParallaxGenUtil::toLowerASCII(relPath.wstring())] = modPtr;
    }

    // delete any mods from file map that were not found
    for (auto it = m_modMap.begin(); it != m_modMap.end();) {
        if (!foundMods.contains(it->second->name)) {
            it = m_modMap.erase(it);
        } else {
            ++it;
        }
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
    unordered_set<wstring> foundMods;
    unordered_map<filesystem::path, unordered_set<wstring>> tempLooseFileConflictTracker;

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

        auto modPtr = createOrGetNewModPtr(mod);
        modPtr->modManagerOrder = basePriority++;

        m_modMap[mod] = modPtr;
        foundMods.insert(mod);

        // loop through each folder to map
        for (const auto& folder : PGGlobals::s_foldersToMap) {
            const auto curSearchDir = curModDir / folder;
            if (!filesystem::exists(curSearchDir)) {
                // skip if folder doesn't exist
                continue;
            }

            try {
                for (auto it = filesystem::recursive_directory_iterator(
                         curSearchDir, filesystem::directory_options::skip_permission_denied);
                    it != filesystem::recursive_directory_iterator(); ++it) {
                    const auto& file = *it;

                    /*if (BethesdaDirectory::isHidden(file.path())) {
                        if (file.is_directory()) {
                            // If it's a directory, don't recurse into it
                            it.disable_recursion_pending();
                        }
                        continue;
                    }*/

                    if (!filesystem::is_regular_file(file)) {
                        continue;
                    }

                    // skip meta.ini file
                    if (boost::iequals(file.path().filename().wstring(), L"meta.ini")) {
                        continue;
                    }

                    auto relPath = filesystem::relative(file, curModDir);
                    const filesystem::path relPathLower = ParallaxGenUtil::toLowerASCII(relPath.wstring());

                    // add to temp loose file tracker
                    tempLooseFileConflictTracker[relPathLower].insert(mod);
                    for (const auto& curMod : tempLooseFileConflictTracker[relPathLower]) {
                        if (curMod == mod) {
                            continue; // skip if same mod
                        }

                        const auto& curModPtr = createOrGetNewModPtr(curMod);

                        // add to conflict tracker to indicate that this mod conflicts with these other mods
                        const lock_guard<mutex> lock(m_modLooseFileConflictsMutex);
                        m_modLooseFileConflicts[modPtr].insert(curModPtr);
                        m_modLooseFileConflicts[curModPtr].insert(modPtr);
                    }

                    // check if already in map
                    if (m_modFileMap.contains(relPathLower)) {
                        // we skip here because this is a losing override in loose files
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

    // delete any mods from file map that were not found
    for (auto it = m_modMap.begin(); it != m_modMap.end();) {
        if (!foundMods.contains(it->second->name)) {
            it = m_modMap.erase(it);
        } else {
            ++it;
        }
    }

    if (!foundOneMod) {
        spdlog::critical(L"MO2 modlist.txt was empty, no mods found");
        exit(1);
    }

    modListFileF.close();
}

auto ModManagerDirectory::doModsConflictInLooseFiles(const std::shared_ptr<Mod>& mod1, const std::shared_ptr<Mod>& mod2)
    -> bool
{
    const lock_guard<mutex> lock(m_modLooseFileConflictsMutex);
    return (m_modLooseFileConflicts.contains(mod1) && m_modLooseFileConflicts[mod1].contains(mod2))
        || (m_modLooseFileConflicts.contains(mod2) && m_modLooseFileConflicts[mod2].contains(mod1));
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

    if (baseDir.empty()) {
        // if baseDir is empty, set it to the instance directory
        baseDir = instanceDir;
    }

    // replace any instance of %BASE_DIR% with the base directory
    boost::replace_all(profileDirField, MO2INI_BASEDIR_WILDCARD, baseDir.wstring());
    boost::replace_all(modDirField, MO2INI_BASEDIR_WILDCARD, baseDir.wstring());

    filesystem::path profileDir = profileDirField;
    filesystem::path modDir = modDirField;

    if (profileDir.empty()) {
        profileDir = baseDir / "profiles";
    }

    if (modDir.empty()) {
        modDir = baseDir / "mods";
    }

    return { profileDir, modDir };
}

auto ModManagerDirectory::getVortexPath() -> std::filesystem::path
{
    const string vortexRegPath = R"(SOFTWARE\57979c68-f490-55b8-8fed-8b017a5af2fe)";

    std::vector<char> data(REG_BUFFER_SIZE, '\0');
    DWORD dataSize = REG_BUFFER_SIZE;

    const LONG result = RegGetValueA(
        HKEY_LOCAL_MACHINE, vortexRegPath.c_str(), "InstallLocation", RRF_RT_REG_SZ, nullptr, data.data(), &dataSize);
    if (result == ERROR_SUCCESS) {
        return { data.data() };
    }

    return {};
}

auto ModManagerDirectory::createOrGetNewModPtr(const wstring& modName) -> shared_ptr<Mod>
{
    if (m_modMap.contains(modName)) {
        return m_modMap.at(modName);
    }

    auto modPtr = make_shared<Mod>();
    modPtr->name = modName;
    m_modMap[modName] = modPtr;

    return modPtr;
}
