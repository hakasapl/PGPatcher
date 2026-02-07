#include "ParallaxGenConfig.hpp"

#include "BethesdaGame.hpp"
#include "ModManagerDirectory.hpp"
#include "PGGlobals.hpp"
#include "ParallaxGenPlugin.hpp"
#include "util/Logger.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>
#include <spdlog/spdlog.h>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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
    static const filesystem::path userConfigFile = s_exePath / "cfg" / "settings.json";
    return userConfigFile;
}

auto ParallaxGenConfig::getModConfigFile() -> filesystem::path
{
    if (s_exePath.empty()) {
        throw runtime_error("ExePath not set");
    }

    // Get mod config file
    static const filesystem::path modConfigFile = s_exePath / "cfg" / "modrules.json";
    return modConfigFile;
}

auto ParallaxGenConfig::getIgnoredMessagesConfigFile() -> filesystem::path
{
    if (s_exePath.empty()) {
        throw runtime_error("ExePath not set");
    }

    // Get ignored messages config file
    static const filesystem::path ignoredMessagesConfigFile = s_exePath / "cfg" / "ignored_messages.json";
    return ignoredMessagesConfigFile;
}

auto ParallaxGenConfig::getDefaultParams() -> PGParams
{
    PGParams outParams;

    // Game
    outParams.Game.dir = BethesdaGame::findGamePathFromSteam(BethesdaGame::GameType::SKYRIM_SE);

    // Mesh Rules
    static const vector<wstring> defaultMeshBlocklist
        = { L"*\\cameras\\*", L"*\\dyndolod\\*", L"*\\lod\\*", L"*_lod_*", L"*_lod.*", L"*\\markers\\*" };
    outParams.Processing.blockList = defaultMeshBlocklist;

    // Texture Rules
    static const vector<wstring> defaultVanillaBSAList = { L"Skyrim - Textures0.bsa", L"Skyrim - Textures1.bsa",
        L"Skyrim - Textures2.bsa", L"Skyrim - Textures3.bsa", L"Skyrim - Textures4.bsa", L"Skyrim - Textures5.bsa",
        L"Skyrim - Textures6.bsa", L"Skyrim - Textures7.bsa", L"Skyrim - Textures8.bsa",
        L"Project Clarity AIO Half Res Packed.bsa", L"Project Clarity AIO Half Res Packed - Textures.bsa",
        L"Project Clarity AIO Half Res Packed0 - Textures.bsa", L"Project Clarity AIO Half Res Packed1 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed2 - Textures.bsa", L"Project Clarity AIO Half Res Packed3 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed4 - Textures.bsa", L"Project Clarity AIO Half Res Packed5 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed6 - Textures.bsa" };
    outParams.Processing.vanillaBSAList = defaultVanillaBSAList;

    return outParams;
}

void ParallaxGenConfig::loadConfig()
{
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
        }
    }

    if (!loadedConfig) {
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
        if (paramJ.contains("output") && paramJ["output"].contains("pluginlang")) {
            m_params.Output.pluginLang
                = ParallaxGenPlugin::getPluginLangFromString(paramJ["output"]["pluginlang"].get<string>());
        }

        // "processing"
        if (paramJ.contains("processing") && paramJ["processing"].contains("multithread")) {
            paramJ["processing"]["multithread"].get_to<bool>(m_params.Processing.multithread);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("pluginesmify")) {
            paramJ["processing"]["pluginesmify"].get_to<bool>(m_params.Processing.pluginESMify);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("devmode")) {
            paramJ["processing"]["devmode"].get_to<bool>(m_params.Processing.enableModDevMode);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("enabledebuglogging")) {
            paramJ["processing"]["enabledebuglogging"].get_to<bool>(m_params.Processing.enableDebugLogging);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("enabletracelogging")) {
            paramJ["processing"]["enabletracelogging"].get_to<bool>(m_params.Processing.enableTraceLogging);
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("allowlist")) {
            for (const auto& item : paramJ["processing"]["allowlist"]) {
                m_params.Processing.allowList.push_back(utf8toUTF16(item.get<string>()));
            }
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("blocklist")) {
            for (const auto& item : paramJ["processing"]["blocklist"]) {
                m_params.Processing.blockList.push_back(utf8toUTF16(item.get<string>()));
            }
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("texturemaps")) {
            for (const auto& item : paramJ["processing"]["texturemaps"].items()) {
                m_params.Processing.textureMaps.emplace_back(
                    utf8toUTF16(item.key()), NIFUtil::getTexTypeFromStr(item.value().get<string>()));
            }
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("vanillabsalist")) {
            for (const auto& item : paramJ["processing"]["vanillabsalist"]) {
                m_params.Processing.vanillaBSAList.push_back(utf8toUTF16(item.get<string>()));
            }
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("allowedmodelrecordtypes")) {
            m_params.Processing.allowedModelRecordTypes.clear();

            for (const auto& item : paramJ["processing"]["allowedmodelrecordtypes"]) {
                m_params.Processing.allowedModelRecordTypes.insert(
                    ParallaxGenPlugin::getRecTypeFromString(item.get<string>()));
            }
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
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr")) {
            paramJ["shaderpatcher"]["truepbr"].get_to<bool>(m_params.ShaderPatcher.truePBR);
        }

        // "shadertransforms"
        if (paramJ.contains("shadertransforms") && paramJ["shadertransforms"].contains("parallaxtocm")) {
            paramJ["shadertransforms"]["parallaxtocm"].get_to<bool>(m_params.ShaderTransforms.parallaxToCM);
        }

        // "postpatcher"
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("disableprepatchedmaterials")) {
            paramJ["postpatcher"]["disableprepatchedmaterials"].get_to<bool>(
                m_params.PostPatcher.disablePrePatchedMaterials);
        }
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

    // Shader Transforms
    if (params.ShaderTransforms.parallaxToCM
        && (!params.ShaderPatcher.parallax || !params.ShaderPatcher.complexMaterial)) {
        errors.emplace_back(
            "Upgrade Parallax to Complex Material requires both the Complex Material and Parallax shader patchers");
    }

    // Post-Patchers

    // Mesh Rules
    checkSet.clear();
    for (const auto& item : params.Processing.allowList) {
        if (item.empty()) {
            errors.emplace_back("Empty entry in Mesh Allow List");
        }

        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Mesh Allow List: " + utf16toUTF8(item));
        }
    }

    checkSet.clear();
    for (const auto& item : params.Processing.blockList) {
        if (item.empty()) {
            errors.emplace_back("Empty entry in Mesh Block List");
        }

        if (!checkSet.insert(item).second) {
            errors.emplace_back("Duplicate entry in Mesh Block List: " + utf16toUTF8(item));
        }
    }

    // Texture Rules

    checkSet.clear();
    for (const auto& [key, value] : params.Processing.textureMaps) {
        if (key.empty()) {
            errors.emplace_back("Empty key in Texture Rules");
        }

        if (!checkSet.insert(key).second) {
            errors.emplace_back("Duplicate entry in Texture Rules: " + utf16toUTF8(key));
        }
    }

    checkSet.clear();
    for (const auto& item : params.Processing.vanillaBSAList) {
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
    j["params"]["output"]["pluginlang"] = ParallaxGenPlugin::getStringFromPluginLang(m_params.Output.pluginLang);

    // "processing"
    j["params"]["processing"]["multithread"] = m_params.Processing.multithread;
    j["params"]["processing"]["pluginesmify"] = m_params.Processing.pluginESMify;
    j["params"]["processing"]["devmode"] = m_params.Processing.enableModDevMode;
    j["params"]["processing"]["enabledebuglogging"] = m_params.Processing.enableDebugLogging;
    j["params"]["processing"]["enabletracelogging"] = m_params.Processing.enableTraceLogging;
    j["params"]["processing"]["allowlist"] = utf16VectorToUTF8(m_params.Processing.allowList);
    j["params"]["processing"]["blocklist"] = utf16VectorToUTF8(m_params.Processing.blockList);
    j["params"]["processing"]["texturemaps"] = nlohmann::json::object();
    for (const auto& [Key, Value] : m_params.Processing.textureMaps) {
        j["params"]["processing"]["texturemaps"][utf16toUTF8(Key)] = NIFUtil::getStrFromTexType(Value);
    }
    j["params"]["processing"]["vanillabsalist"] = utf16VectorToUTF8(m_params.Processing.vanillaBSAList);
    j["params"]["processing"]["allowedmodelrecordtypes"] = nlohmann::json::array();
    for (const auto& item : m_params.Processing.allowedModelRecordTypes) {
        j["params"]["processing"]["allowedmodelrecordtypes"].push_back(ParallaxGenPlugin::getStringFromRecType(item));
    }

    // "prepatcher"
    j["params"]["prepatcher"]["disablemlp"] = m_params.PrePatcher.disableMLP;
    j["params"]["prepatcher"]["fixmeshlighting"] = m_params.PrePatcher.fixMeshLighting;

    // "shaderpatcher"
    j["params"]["shaderpatcher"]["parallax"] = m_params.ShaderPatcher.parallax;
    j["params"]["shaderpatcher"]["complexmaterial"] = m_params.ShaderPatcher.complexMaterial;
    j["params"]["shaderpatcher"]["truepbr"] = m_params.ShaderPatcher.truePBR;

    // "shadertransforms"
    j["params"]["shadertransforms"]["parallaxtocm"] = m_params.ShaderTransforms.parallaxToCM;

    // "postpatcher"
    j["params"]["postpatcher"]["disableprepatchedmaterials"] = m_params.PostPatcher.disablePrePatchedMaterials;
    j["params"]["postpatcher"]["fixsss"] = m_params.PostPatcher.fixSSS;
    j["params"]["postpatcher"]["hairflowmap"] = m_params.PostPatcher.hairFlowMap;

    // "globalpatcher"
    j["params"]["globalpatcher"]["fixeffectlightingcs"] = m_params.GlobalPatcher.fixEffectLightingCS;

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

auto ParallaxGenConfig::getIgnoredMessagesConfig() -> std::unordered_map<wxString, bool>
{
    std::unordered_map<wxString, bool> ignoredItems;

    if (!filesystem::exists(getIgnoredMessagesConfigFile())) {
        return ignoredItems;
    }

    nlohmann::json j;
    if (!parseJSON(getFileBytes(getIgnoredMessagesConfigFile()), j)) {
        throw std::runtime_error("Failed to parse ignored messages config JSON");
    }

    if (!j.contains("ignored_messages") || !j["ignored_messages"].is_object()) {
        throw std::runtime_error("Invalid ignored messages config JSON format");
    }

    for (const auto& item : j["ignored_messages"].items()) {
        ignoredItems[wxString::FromUTF8(item.key().c_str())] = item.value().get<bool>();
    }

    return ignoredItems;
}

auto ParallaxGenConfig::saveIgnoredMessagesConfig(const std::unordered_map<wxString, bool>& ignoredItems) -> bool
{
    nlohmann::json j;
    j["ignored_messages"] = nlohmann::json::object();

    for (const auto& [key, value] : ignoredItems) {
        j["ignored_messages"][std::string(key.ToUTF8().data())] = value;
    }

    // write to file
    try {
        filesystem::create_directories(getIgnoredMessagesConfigFile().parent_path());
        ParallaxGenUtil::saveJSON(getIgnoredMessagesConfigFile(), j, true);
    } catch (const std::exception& e) {
        spdlog::error("Failed to save ignored messages config: {}", e.what());
        return false;
    }

    return true;
}
