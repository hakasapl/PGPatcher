#include "ParallaxGenConfig.hpp"

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "PGGlobals.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <exception>
#include <filesystem>
#include <string>

using namespace std;
using namespace ParallaxGenUtil;

// Statics
filesystem::path ParallaxGenConfig::s_exePath;

void ParallaxGenConfig::loadStatics(const filesystem::path& exePath)
{
    // Set ExePath
    ParallaxGenConfig::s_exePath = exePath;
}

auto ParallaxGenConfig::getUserConfigFile() -> filesystem::path
{
    if (s_exePath.empty()) {
        throw runtime_error("ExePath not set");
    }

    // Get user config file
    static const filesystem::path userConfigFile = s_exePath / "cfg" / "user.json";
    return userConfigFile;
}

auto ParallaxGenConfig::getModConfigFile() -> filesystem::path
{
    if (s_exePath.empty()) {
        throw runtime_error("ExePath not set");
    }

    // Get mod config file
    static const filesystem::path modConfigFile = s_exePath / "cfg" / "mods.json";
    return modConfigFile;
}

auto ParallaxGenConfig::getDefaultParams() -> PGParams
{
    PGParams outParams;

    // Game
    outParams.Game.dir = BethesdaGame::findGamePathFromSteam(BethesdaGame::GameType::SKYRIM_SE);

    // Output
    if (s_exePath.empty()) {
        throw runtime_error("ExePath not set");
    }
    outParams.Output.dir = s_exePath / "ParallaxGen_Output";

    // Mesh Rules
    static const vector<wstring> defaultMeshBlocklist
        = { L"*\\cameras\\*", L"*\\dyndolod\\*", L"*\\lod\\*", L"*\\markers\\*" };
    outParams.MeshRules.blockList = defaultMeshBlocklist;

    // Texture Rules
    static const vector<wstring> defaultVanillaBSAList = { L"Skyrim - Textures0.bsa", L"Skyrim - Textures1.bsa",
        L"Skyrim - Textures2.bsa", L"Skyrim - Textures3.bsa", L"Skyrim - Textures4.bsa", L"Skyrim - Textures5.bsa",
        L"Skyrim - Textures6.bsa", L"Skyrim - Textures7.bsa", L"Skyrim - Textures8.bsa",
        L"Project Clarity AIO Half Res Packed.bsa", L"Project Clarity AIO Half Res Packed - Textures.bsa",
        L"Project Clarity AIO Half Res Packed0 - Textures.bsa", L"Project Clarity AIO Half Res Packed1 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed2 - Textures.bsa", L"Project Clarity AIO Half Res Packed3 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed4 - Textures.bsa", L"Project Clarity AIO Half Res Packed5 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed6 - Textures.bsa" };
    outParams.TextureRules.vanillaBSAList = defaultVanillaBSAList;

    return outParams;
}

void ParallaxGenConfig::loadConfig()
{
    const Logger::Prefix prefix("PGC/loadConfig");

    bool loadedConfig = false;
    if (filesystem::exists(getUserConfigFile())) {
        // don't load a config that doesn't exist
        Logger::debug(L"Loading ParallaxGen Config: {}", getUserConfigFile().wstring());

        nlohmann::json j;
        if (parseJSON(getFileBytes(getUserConfigFile()), j)) {
            if (!j.empty()) {
                replaceForwardSlashes(j);
                addConfigJSON(j);
            }

            m_userConfig = j;
            loadedConfig = true;
            Logger::info("Loaded user config successfully");
        } else {
            Logger::error("Failed to parse ParallaxGen config file");
        }
    }

    if (!loadedConfig) {
        Logger::warn("No user config found, using defaults");
        m_params = getDefaultParams();
    }
}

auto ParallaxGenConfig::addConfigJSON(const nlohmann::json& j) -> void
{
    // "params" field
    if (j.contains("params")) {
        const auto& paramJ = j["params"];

        // "game"
        if (paramJ.contains("game") && paramJ["game"].contains("dir")) {
            m_params.Game.dir = utf8toUTF16(paramJ["game"]["dir"].get<string>());
        }
        if (paramJ.contains("game") && paramJ["game"].contains("type")) {
            paramJ["game"]["type"].get_to<BethesdaGame::GameType>(m_params.Game.type);
        }

        // "modmanager"
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("type")) {
            paramJ["modmanager"]["type"].get_to<ModManagerDirectory::ModManagerType>(m_params.ModManager.type);
        }
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2instancedir")) {
            paramJ["modmanager"]["mo2instancedir"].get_to<filesystem::path>(m_params.ModManager.mo2InstanceDir);
        }
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2useloosefileorder")) {
            paramJ["modmanager"]["mo2useloosefileorder"].get_to<bool>(m_params.ModManager.mo2UseLooseFileOrder);
        }

        // "output"
        if (paramJ.contains("output") && paramJ["output"].contains("dir")) {
            m_params.Output.dir = utf8toUTF16(paramJ["output"]["dir"].get<string>());
        }
        if (paramJ.contains("output") && paramJ["output"].contains("zip")) {
            paramJ["output"]["zip"].get_to<bool>(m_params.Output.zip);
        }

        // "advanced"
        if (paramJ.contains("advanced")) {
            paramJ["advanced"].get_to<bool>(m_params.advanced);
        }

        // "processing"
        if (paramJ.contains("processing") && paramJ["processing"].contains("multithread")) {
            paramJ["processing"]["multithread"].get_to<bool>(m_params.Processing.multithread);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("highmem")) {
            paramJ["processing"]["highmem"].get_to<bool>(m_params.Processing.highMem);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("bsa")) {
            paramJ["processing"]["bsa"].get_to<bool>(m_params.Processing.bsa);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("pluginpatching")) {
            paramJ["processing"]["pluginpatching"].get_to<bool>(m_params.Processing.pluginPatching);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("pluginesmify")) {
            paramJ["processing"]["pluginesmify"].get_to<bool>(m_params.Processing.pluginESMify);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("pluginlang")) {
            m_params.Processing.pluginLang
                = ParallaxGenPlugin::getPluginLangFromString(paramJ["processing"]["pluginlang"].get<string>());
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("diagnostics")) {
            paramJ["processing"]["diagnostics"].get_to<bool>(m_params.Processing.diagnostics);
        }

        // "prepatcher"
        if (paramJ.contains("prepatcher") && paramJ["prepatcher"].contains("disablemlp")) {
            paramJ["prepatcher"]["disablemlp"].get_to<bool>(m_params.PrePatcher.disableMLP);
        }
        if (paramJ.contains("prepatcher") && paramJ["prepatcher"].contains("fixmeshlighting")) {
            paramJ["prepatcher"]["fixmeshlighting"].get_to<bool>(m_params.PrePatcher.fixMeshLighting);
        }

        // "shaderpatcher"
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("parallax")) {
            paramJ["shaderpatcher"]["parallax"].get_to<bool>(m_params.ShaderPatcher.parallax);
        }
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("complexmaterial")) {
            paramJ["shaderpatcher"]["complexmaterial"].get_to<bool>(m_params.ShaderPatcher.complexMaterial);
        }
        if (paramJ.contains("shaderpatcher")
            && paramJ["shaderpatcher"].contains("complexmaterialdyncubemapblocklist")) {
            for (const auto& item : paramJ["shaderpatcher"]["complexmaterialdyncubemapblocklist"]) {
                m_params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist.push_back(
                    utf8toUTF16(item.get<string>()));
            }
        }
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr")) {
            paramJ["shaderpatcher"]["truepbr"].get_to<bool>(m_params.ShaderPatcher.truePBR);
        }
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr_checkpaths")) {
            paramJ["shaderpatcher"]["truepbr_checkpaths"].get_to<bool>(
                m_params.ShaderPatcher.ShaderPatcherTruePBR.checkPaths);
        }
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr_printnonexistentpaths")) {
            paramJ["shaderpatcher"]["truepbr_printnonexistentpaths"].get_to<bool>(
                m_params.ShaderPatcher.ShaderPatcherTruePBR.printNonExistentPaths);
        }

        // "shadertransforms"
        if (paramJ.contains("shadertransforms") && paramJ["shadertransforms"].contains("parallaxtocm")) {
            paramJ["shadertransforms"]["parallaxtocm"].get_to<bool>(m_params.ShaderTransforms.parallaxToCM);
        }

        // "postpatcher"
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("fixsss")) {
            paramJ["postpatcher"]["fixsss"].get_to<bool>(m_params.PostPatcher.fixSSS);
        }
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("hairflowmap")) {
            paramJ["postpatcher"]["hairflowmap"].get_to<bool>(m_params.PostPatcher.hairFlowMap);
        }

        // "globalpatcher"
        if (paramJ.contains("globalpatcher") && paramJ["globalpatcher"].contains("fixeffectlightingcs")) {
            paramJ["globalpatcher"]["fixeffectlightingcs"].get_to<bool>(m_params.GlobalPatcher.fixEffectLightingCS);
        }

        // "meshrules"
        if (paramJ.contains("meshrules") && paramJ["meshrules"].contains("allowlist")) {
            for (const auto& item : paramJ["meshrules"]["allowlist"]) {
                m_params.MeshRules.allowList.push_back(utf8toUTF16(item.get<string>()));
            }
        }

        if (paramJ.contains("meshrules") && paramJ["meshrules"].contains("blocklist")) {
            for (const auto& item : paramJ["meshrules"]["blocklist"]) {
                m_params.MeshRules.blockList.push_back(utf8toUTF16(item.get<string>()));
            }
        }

        // "texturerules"
        if (paramJ.contains("texturerules") && paramJ["texturerules"].contains("texturemaps")) {
            for (const auto& item : paramJ["texturerules"]["texturemaps"].items()) {
                m_params.TextureRules.textureMaps.emplace_back(
                    utf8toUTF16(item.key()), NIFUtil::getTexTypeFromStr(item.value().get<string>()));
            }
        }

        if (paramJ.contains("texturerules") && paramJ["texturerules"].contains("vanillabsalist")) {
            for (const auto& item : paramJ["texturerules"]["vanillabsalist"]) {
                m_params.TextureRules.vanillaBSAList.push_back(utf8toUTF16(item.get<string>()));
            }
        }
    }
}

auto ParallaxGenConfig::parseJSON(const vector<std::byte>& bytes, nlohmann::json& j) -> bool
{
    // Parse JSON
    try {
        j = nlohmann::json::parse(bytes);
    } catch (...) {
        j = {};
        return false;
    }

    return true;
}

void ParallaxGenConfig::replaceForwardSlashes(nlohmann::json& json)
{
    if (json.is_string()) {
        auto& str = json.get_ref<string&>();
        for (auto& ch : str) {
            if (ch == '/') {
                ch = '\\';
            }
        }
    } else if (json.is_object()) {
        for (const auto& item : json.items()) {
            replaceForwardSlashes(item.value());
        }
    } else if (json.is_array()) {
        for (auto& element : json) {
            replaceForwardSlashes(element);
        }
    }
}

auto ParallaxGenConfig::getParams() const -> PGParams { return m_params; }

void ParallaxGenConfig::setParams(const PGParams& params) { this->m_params = params; }

auto ParallaxGenConfig::validateParams(const PGParams& params, vector<string>& errors) -> bool
{
    // Helpers
    unordered_set<wstring> checkSet;

    // Game
    if (params.Game.dir.empty()) {
        errors.emplace_back("Game Location is required.");
    }

    if (!BethesdaGame::isGamePathValid(params.Game.dir, params.Game.type)) {
        errors.emplace_back("Game Location is not valid.");
    }

    // Mod Manager
    if (params.ModManager.type == ModManagerDirectory::ModManagerType::MODORGANIZER2) {
        if (params.ModManager.mo2InstanceDir.empty()) {
            errors.emplace_back("MO2 Instance Location is required");
        }

        if (!filesystem::exists(params.ModManager.mo2InstanceDir)) {
            errors.emplace_back("MO2 Instance Location does not exist");
        }

        if (!ModManagerDirectory::isValidMO2InstanceDir(params.ModManager.mo2InstanceDir)) {
            errors.emplace_back("MO2 Instance Location is not valid");
        }
    }

    // Output
    if (params.Output.dir.empty()) {
        errors.emplace_back("Output Location is required");
    }

    // Processing

    // Pre-Patchers

    // Shader Patchers

    checkSet.clear();
    for (const auto& item : params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist) {
        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Complex Material Dyncubemap Block List: " + utf16toUTF8(item));
        }
    }

    // Shader Transforms
    if (params.ShaderTransforms.parallaxToCM
        && (!params.ShaderPatcher.parallax || !params.ShaderPatcher.complexMaterial)) {
        errors.emplace_back(
            "Upgrade Parallax to Complex Material requires both the Complex Material and Parallax shader patchers");
    }

    // Post-Patchers

    // Mesh Rules
    checkSet.clear();
    for (const auto& item : params.MeshRules.allowList) {
        if (item.empty()) {
            errors.emplace_back("Empty entry in Mesh Allow List");
        }

        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Mesh Allow List: " + utf16toUTF8(item));
        }
    }

    checkSet.clear();
    for (const auto& item : params.MeshRules.blockList) {
        if (item.empty()) {
            errors.emplace_back("Empty entry in Mesh Block List");
        }

        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Mesh Block List: " + utf16toUTF8(item));
        }
    }

    // Texture Rules

    checkSet.clear();
    for (const auto& [key, value] : params.TextureRules.textureMaps) {
        if (key.empty()) {
            errors.emplace_back("Empty key in Texture Rules");
        }

        if (!checkSet.insert(key).second) {
            errors.emplace_back("Duplicate entry in Texture Rules: " + utf16toUTF8(key));
        }
    }

    checkSet.clear();
    for (const auto& item : params.TextureRules.vanillaBSAList) {
        if (item.empty()) {
            errors.emplace_back("Empty entry in Vanilla BSA List");
        }

        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Vanilla BSA List: " + utf16toUTF8(item));
        }
    }

    return errors.empty();
}

auto ParallaxGenConfig::getUserConfigJSON() const -> nlohmann::json
{
    // build output json
    nlohmann::json j = m_userConfig;

    // Params

    // "game"
    j["params"]["game"]["dir"] = utf16toUTF8(m_params.Game.dir.wstring());
    j["params"]["game"]["type"] = m_params.Game.type;

    // "modmanager"
    j["params"]["modmanager"]["type"] = m_params.ModManager.type;
    j["params"]["modmanager"]["mo2instancedir"] = utf16toUTF8(m_params.ModManager.mo2InstanceDir.wstring());
    j["params"]["modmanager"]["mo2useloosefileorder"] = m_params.ModManager.mo2UseLooseFileOrder;

    // "output"
    j["params"]["output"]["dir"] = utf16toUTF8(m_params.Output.dir.wstring());
    j["params"]["output"]["zip"] = m_params.Output.zip;

    // "advanced"
    j["params"]["advanced"] = m_params.advanced;

    // "processing"
    j["params"]["processing"]["multithread"] = m_params.Processing.multithread;
    j["params"]["processing"]["highmem"] = m_params.Processing.highMem;
    j["params"]["processing"]["bsa"] = m_params.Processing.bsa;
    j["params"]["processing"]["pluginpatching"] = m_params.Processing.pluginPatching;
    j["params"]["processing"]["pluginesmify"] = m_params.Processing.pluginESMify;
    j["params"]["processing"]["pluginlang"]
        = ParallaxGenPlugin::getStringFromPluginLang(m_params.Processing.pluginLang);
    j["params"]["processing"]["diagnostics"] = m_params.Processing.diagnostics;

    // "prepatcher"
    j["params"]["prepatcher"]["disablemlp"] = m_params.PrePatcher.disableMLP;
    j["params"]["prepatcher"]["fixmeshlighting"] = m_params.PrePatcher.fixMeshLighting;

    // "shaderpatcher"
    j["params"]["shaderpatcher"]["parallax"] = m_params.ShaderPatcher.parallax;
    j["params"]["shaderpatcher"]["complexmaterial"] = m_params.ShaderPatcher.complexMaterial;
    j["params"]["shaderpatcher"]["complexmaterialdyncubemapblocklist"]
        = utf16VectorToUTF8(m_params.ShaderPatcher.ShaderPatcherComplexMaterial.listsDyncubemapBlocklist);
    j["params"]["shaderpatcher"]["truepbr_checkpaths"] = m_params.ShaderPatcher.ShaderPatcherTruePBR.checkPaths;
    j["params"]["shaderpatcher"]["truepbr_printnonexistentpaths"]
        = m_params.ShaderPatcher.ShaderPatcherTruePBR.printNonExistentPaths;
    j["params"]["shaderpatcher"]["truepbr"] = m_params.ShaderPatcher.truePBR;

    // "shadertransforms"
    j["params"]["shadertransforms"]["parallaxtocm"] = m_params.ShaderTransforms.parallaxToCM;

    // "postpatcher"
    j["params"]["postpatcher"]["fixsss"] = m_params.PostPatcher.fixSSS;
    j["params"]["postpatcher"]["hairflowmap"] = m_params.PostPatcher.hairFlowMap;

    // "globalpatcher"
    j["params"]["globalpatcher"]["fixeffectlightingcs"] = m_params.GlobalPatcher.fixEffectLightingCS;

    // "meshrules"
    j["params"]["meshrules"]["allowlist"] = utf16VectorToUTF8(m_params.MeshRules.allowList);
    j["params"]["meshrules"]["blocklist"] = utf16VectorToUTF8(m_params.MeshRules.blockList);

    // "texturerules"
    j["params"]["texturerules"]["texturemaps"] = nlohmann::json::object();
    for (const auto& [Key, Value] : m_params.TextureRules.textureMaps) {
        j["params"]["texturerules"]["texturemaps"][utf16toUTF8(Key)] = NIFUtil::getStrFromTexType(Value);
    }
    j["params"]["texturerules"]["vanillabsalist"] = utf16VectorToUTF8(m_params.TextureRules.vanillaBSAList);

    return j;
}

auto ParallaxGenConfig::saveUserConfig() -> bool
{
    const auto j = getUserConfigJSON();

    // Update UserConfig var
    m_userConfig = j;

    // write to file
    try {
        filesystem::create_directories(getUserConfigFile().parent_path());
        ParallaxGenUtil::saveJSON(getUserConfigFile(), j, true);
    } catch (const exception& e) {
        spdlog::error("Failed to save user config: {}", e.what());
        return false;
    }

    return true;
}

auto ParallaxGenConfig::saveModConfig() -> bool
{
    // Mods
    auto* mmd = PGGlobals::getMMD();
    if (mmd == nullptr) {
        throw runtime_error("Mod Manager Directory not set");
    }

    const auto j = mmd->getJSON();

    // write to file
    try {
        filesystem::create_directories(getModConfigFile().parent_path());
        ParallaxGenUtil::saveJSON(getModConfigFile(), j, true);
    } catch (const exception& e) {
        spdlog::error("Failed to save mod config: {}", e.what());
        return false;
    }

    return true;
}
