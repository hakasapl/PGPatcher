#include "PGConfig.hpp"

#include "PGGlobals.hpp"
#include "PGLocale.hpp"
#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

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

using namespace StringUtil;

// Statics.
std::filesystem::path PGConfig::s_exePath;

void PGConfig::loadStatics(const std::filesystem::path& exePath)
{
    // Set ExePath.
    PGConfig::s_exePath = exePath;
}

std::filesystem::path PGConfig::userConfigFile()
{
    if (s_exePath.empty())
        throw std::runtime_error("ExePath not set");

    // Get user config file.
    static const std::filesystem::path userConfigFile = s_exePath / "cfg" / "settings.json";
    return userConfigFile;
}

std::filesystem::path PGConfig::modConfigFile()
{
    if (s_exePath.empty())
        throw std::runtime_error("ExePath not set");

    // Get mod config file.
    static const std::filesystem::path modConfigFile = s_exePath / "cfg" / "modrules.json";
    return modConfigFile;
}

std::filesystem::path PGConfig::ignoredMessagesConfigFile()
{
    if (s_exePath.empty())
        throw std::runtime_error("ExePath not set");

    // Get ignored messages config file.
    static const std::filesystem::path ignoredMessagesConfigFile = s_exePath / "cfg" / "ignored_messages.json";
    return ignoredMessagesConfigFile;
}

std::filesystem::path PGConfig::resolveExeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute() || s_exePath.empty())
        return path;

    auto resolved = (s_exePath / path).lexically_normal();
    if (!resolved.has_filename()) {
        // Drop the trailing separator left behind by values such as "." or "..\MO2\".
        resolved = resolved.parent_path();
    }

    return resolved;
}

void PGConfig::resolveRelativePaths(PGParams& params)
{
    params.modManager.mo2InstanceDir = resolveExeRelativePath(params.modManager.mo2InstanceDir);
    params.output.dir = resolveExeRelativePath(params.output.dir);

    // The game location is only user-editable when MO2 does not provide it. A game path from modorganizer.ini is.
    // relative to the MO2 folder instead and is resolved by PGModManager::resolveMO2GamePath when it is read.
    const bool gameDirFromMO2 = params.modManager.type == PGModManager::ModManagerType::MODORGANIZER2
        && !PGModManager::gamePathFromInstanceDir(params.modManager.mo2InstanceDir).empty();
    if (!gameDirFromMO2)
        params.game.dir = resolveExeRelativePath(params.game.dir);
}

auto PGConfig::defaultParams() -> PGParams
{
    PGParams outParams;

    // Game.
    outParams.game.dir = BethesdaGame::findGamePathFromSteam(BethesdaGame::GameType::SkyrimSE);

    // Mesh Rules.
    static const std::vector<std::wstring> defaultMeshBlocklist
        = { L"*\\cameras\\*", L"*\\dyndolod\\*", L"*\\lod\\*", L"*_lod_*", L"*_lod.*", L"*\\markers\\*" };
    outParams.processing.blockList = defaultMeshBlocklist;

    // Texture Rules.
    static const std::vector<std::wstring> defaultVanillaBSAList = {
        L"Skyrim - Textures0.bsa",
        L"Skyrim - Textures1.bsa",
        L"Skyrim - Textures2.bsa",
        L"Skyrim - Textures3.bsa",
        L"Skyrim - Textures4.bsa",
        L"Skyrim - Textures5.bsa",
        L"Skyrim - Textures6.bsa",
        L"Skyrim - Textures7.bsa",
        L"Skyrim - Textures8.bsa",
        L"Project Clarity AIO Half Res Packed.bsa",
        L"Project Clarity AIO Half Res Packed - Textures.bsa",
        L"Project Clarity AIO Half Res Packed0 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed1 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed2 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed3 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed4 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed5 - Textures.bsa",
        L"Project Clarity AIO Half Res Packed6 - Textures.bsa",
    };
    outParams.processing.vanillaBSAList = defaultVanillaBSAList;

    return outParams;
}

void PGConfig::loadConfig()
{
    bool loadedConfig = false;
    if (std::filesystem::exists(userConfigFile())) {
        // Don't load a config that doesn't exist.
        Logger::debug(L"Loading PGPatcher Config: {}", userConfigFile().wstring());

        nlohmann::json j;
        if (parseJSON(FileUtil::fileBytes(userConfigFile()), j)) {
            if (!j.empty()) {
                replaceForwardSlashes(j);
                addConfigJSON(j);
            }

            m_userConfig = j;
            loadedConfig = true;
        }
    }

    if (!loadedConfig)
        m_params = defaultParams();
}

void PGConfig::addConfigJSON(const nlohmann::json& j)
{
    // "ui" field.
    if (j.contains("ui") && j["ui"].contains("language") && j["ui"]["language"].is_string())
        m_uiLanguage = j["ui"]["language"].get<std::string>();

    if (j.contains("ui") && j["ui"].contains("theme") && j["ui"]["theme"].is_string())
        m_uiTheme = j["ui"]["theme"].get<std::string>();

    // "params" field.
    if (j.contains("params")) {
        const auto& paramJ = j["params"];

        // "game".
        if (paramJ.contains("game") && paramJ["game"].contains("dir"))
            m_params.game.dir = utf8toUTF16(paramJ["game"]["dir"].get<std::string>());
        if (paramJ.contains("game") && paramJ["game"].contains("type"))
            paramJ["game"]["type"].get_to<BethesdaGame::GameType>(m_params.game.type);

        // "modmanager".
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("type"))
            paramJ["modmanager"]["type"].get_to<PGModManager::ModManagerType>(m_params.modManager.type);
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2instancedir"))
            paramJ["modmanager"]["mo2instancedir"].get_to<std::filesystem::path>(m_params.modManager.mo2InstanceDir);
        if (paramJ.contains("modmanager") && paramJ["modmanager"].contains("mo2useloosefileorder"))
            paramJ["modmanager"]["mo2useloosefileorder"].get_to<bool>(m_params.modManager.mo2UseLooseFileOrder);

        // "output".
        if (paramJ.contains("output") && paramJ["output"].contains("dir"))
            m_params.output.dir = utf8toUTF16(paramJ["output"]["dir"].get<std::string>());
        if (paramJ.contains("output") && paramJ["output"].contains("zip"))
            paramJ["output"]["zip"].get_to<bool>(m_params.output.zip);
        if (paramJ.contains("output") && paramJ["output"].contains("pluginlang")) {
            m_params.output.pluginLang
                = PGPlugin::pluginLangFromString(paramJ["output"]["pluginlang"].get<std::string>());
        }

        // "processing".
        if (paramJ.contains("processing") && paramJ["processing"].contains("multithread"))
            paramJ["processing"]["multithread"].get_to<bool>(m_params.processing.multithread);
        if (paramJ.contains("processing") && paramJ["processing"].contains("devmode"))
            paramJ["processing"]["devmode"].get_to<bool>(m_params.processing.enableModDevMode);
        if (paramJ.contains("processing") && paramJ["processing"].contains("enabledebuglogging"))
            paramJ["processing"]["enabledebuglogging"].get_to<bool>(m_params.processing.enableDebugLogging);
        if (paramJ.contains("processing") && paramJ["processing"].contains("enabletracelogging"))
            paramJ["processing"]["enabletracelogging"].get_to<bool>(m_params.processing.enableTraceLogging);
        if (paramJ.contains("processing") && paramJ["processing"].contains("allowlist"))
            for (const auto& item : paramJ["processing"]["allowlist"])
                m_params.processing.allowList.push_back(utf8toUTF16(item.get<std::string>()));
        if (paramJ.contains("processing") && paramJ["processing"].contains("blocklist"))
            for (const auto& item : paramJ["processing"]["blocklist"])
                m_params.processing.blockList.push_back(utf8toUTF16(item.get<std::string>()));
        if (paramJ.contains("processing") && paramJ["processing"].contains("texturemaps")) {
            for (const auto& item : paramJ["processing"]["texturemaps"].items()) {
                m_params.processing.textureMaps.emplace_back(utf8toUTF16(item.key()),
                                                             PGEnums::texTypeFromStr(item.value().get<std::string>()));
            }
        }
        if (paramJ.contains("processing") && paramJ["processing"].contains("vanillabsalist"))
            for (const auto& item : paramJ["processing"]["vanillabsalist"])
                m_params.processing.vanillaBSAList.push_back(utf8toUTF16(item.get<std::string>()));
        if (paramJ.contains("processing") && paramJ["processing"].contains("allowedmodelrecordtypes")) {
            m_params.processing.allowedModelRecordTypes.clear();

            for (const auto& item : paramJ["processing"]["allowedmodelrecordtypes"])
                m_params.processing.allowedModelRecordTypes.insert(
                    PGPlugin::recTypeFromString(item.get<std::string>()));
        }

        // "prepatcher".
        if (paramJ.contains("prepatcher") && paramJ["prepatcher"].contains("fixmeshlighting"))
            paramJ["prepatcher"]["fixmeshlighting"].get_to<bool>(m_params.prePatcher.fixMeshLighting);

        // "shaderpatcher".
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("parallax"))
            paramJ["shaderpatcher"]["parallax"].get_to<bool>(m_params.shaderPatcher.parallax);
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("complexmaterial"))
            paramJ["shaderpatcher"]["complexmaterial"].get_to<bool>(m_params.shaderPatcher.complexMaterial);
        if (paramJ.contains("shaderpatcher") && paramJ["shaderpatcher"].contains("truepbr"))
            paramJ["shaderpatcher"]["truepbr"].get_to<bool>(m_params.shaderPatcher.truePBR);

        // "shadertransforms".
        if (paramJ.contains("shadertransforms") && paramJ["shadertransforms"].contains("parallaxtocm"))
            paramJ["shadertransforms"]["parallaxtocm"].get_to<bool>(m_params.shaderTransforms.parallaxToCM);

        // "postpatcher".
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("disableprepatchedmaterials")) {
            paramJ["postpatcher"]["disableprepatchedmaterials"].get_to<bool>(
                m_params.postPatcher.disablePrePatchedMaterials);
        }
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("fixsss"))
            paramJ["postpatcher"]["fixsss"].get_to<bool>(m_params.postPatcher.fixSSS);
        if (paramJ.contains("postpatcher") && paramJ["postpatcher"].contains("hairflowmap"))
            paramJ["postpatcher"]["hairflowmap"].get_to<bool>(m_params.postPatcher.hairFlowMap);

        // "globalpatcher".
    }
}

bool PGConfig::parseJSON(const std::vector<std::byte>& bytes,
                         nlohmann::json& j)
{
    // Parse JSON.
    try {
        j = nlohmann::json::parse(bytes);
    } catch (...) {
        j = { };
        return false;
    }

    return true;
}

void PGConfig::replaceForwardSlashes(nlohmann::json& json)
{
    if (json.is_string()) {
        auto& str = json.get_ref<std::string&>();
        for (auto& ch : str)
            if (ch == '/')
                ch = '\\';
    } else if (json.is_object()) {
        for (const auto& item : json.items())
            replaceForwardSlashes(item.value());
    } else if (json.is_array()) {
        for (auto& element : json)
            replaceForwardSlashes(element);
    }
}

auto PGConfig::params() const -> PGParams { return m_params; }

void PGConfig::setParams(const PGParams& params) { this->m_params = params; }

std::string PGConfig::uiLanguage() const { return m_uiLanguage; }

void PGConfig::setUILanguage(const std::string& lang) { m_uiLanguage = lang; }

std::string PGConfig::uiTheme() const { return m_uiTheme; }

void PGConfig::setUITheme(const std::string& theme) { m_uiTheme = theme; }

bool PGConfig::validateParams(const PGParams& rawParams,
                              std::vector<std::string>& errors)
{
    // Paths may be relative to the PGPatcher.exe folder; validate the resolved ones
    PGParams params = rawParams;
    resolveRelativePaths(params);

    // Helpers.
    std::unordered_set<std::wstring> checkSet;

    // Validation messages are shown in the launcher's error dialog, so they are localized like every other GUI string.
    const auto addError = [&errors](const std::string& key) { errors.emplace_back(pgTr(key).utf8_string()); };
    const auto addErrorWithItem = [&errors](const std::string& key, const std::wstring& item) {
        errors.emplace_back(wxString::Format(pgTr(key), wxString(item)).utf8_string());
    };

    // Game.
    if (params.game.dir.empty())
        addError("launcher.validation.gameLocationRequired");

    if (params.modManager.type == PGModManager::ModManagerType::MODORGANIZER2 && !params.game.dir.empty()
        && params.game.dir.is_relative()) {
        // MO2 stores the game path relative to its own folder, which PGPatcher finds through the MO2 VFS it was.
        // launched from or, for portable instances, the instance folder (see PGModManager::findMO2Dir). Neither worked
        // here: resolveRelativePaths() never resolves a game path that comes from modorganizer.ini against the
        // PGPatcher folder, so a game path that is still relative at this point can only be that unresolved MO2 value.
        addError("launcher.validation.mo2RelativeGamePath");
    } else if (!BethesdaGame::isGamePathValid(params.game.dir, params.game.type)) {
        addError("launcher.validation.gameLocationInvalid");
    }

    // Mod Manager.
    if (params.modManager.type == PGModManager::ModManagerType::MODORGANIZER2) {
        if (params.modManager.mo2InstanceDir.empty())
            addError("launcher.validation.mo2InstanceRequired");

        if (!std::filesystem::exists(params.modManager.mo2InstanceDir))
            addError("launcher.validation.mo2InstanceMissing");

        if (!PGModManager::isValidMO2InstanceDir(params.modManager.mo2InstanceDir))
            addError("launcher.validation.mo2InstanceInvalid");
    }

    // Output.
    if (params.output.dir.empty())
        addError("launcher.validation.outputLocationRequired");

    // Processing.

    // Pre-Patchers.

    // Shader Patchers.

    // Shader Transforms.
    if (params.shaderTransforms.parallaxToCM
        && (!params.shaderPatcher.parallax || !params.shaderPatcher.complexMaterial)) {
        addError("launcher.validation.parallaxToCMRequiresPatchers");
    }

    // Post-Patchers.

    // Mesh Rules.
    checkSet.clear();
    for (const auto& item : params.processing.allowList) {
        if (item.empty())
            addError("launcher.validation.meshAllowListEmptyEntry");

        if (!checkSet.insert(item).second)
            addErrorWithItem("launcher.validation.meshAllowListDuplicate", item);
    }

    checkSet.clear();
    for (const auto& item : params.processing.blockList) {
        if (item.empty())
            addError("launcher.validation.meshBlockListEmptyEntry");

        if (!checkSet.insert(item).second)
            addErrorWithItem("launcher.validation.meshBlockListDuplicate", item);
    }

    // Texture Rules.

    checkSet.clear();
    for (const auto& [key, value] : params.processing.textureMaps) {
        if (key.empty())
            addError("launcher.validation.textureRulesEmptyKey");

        if (!checkSet.insert(key).second)
            addErrorWithItem("launcher.validation.textureRulesDuplicate", key);
    }

    checkSet.clear();
    for (const auto& item : params.processing.vanillaBSAList) {
        if (item.empty())
            addError("launcher.validation.vanillaBSAListEmptyEntry");

        if (!checkSet.insert(item).second)
            addErrorWithItem("launcher.validation.vanillaBSAListDuplicate", item);
    }

    return errors.empty();
}

nlohmann::json PGConfig::userConfigJSON() const
{
    // Build output json.
    nlohmann::json j = m_userConfig;

    // "ui".
    j["ui"]["language"] = m_uiLanguage;
    j["ui"]["theme"] = m_uiTheme;

    // Params.

    // "game".
    j["params"]["game"]["dir"] = utf16toUTF8(m_params.game.dir.wstring());
    j["params"]["game"]["type"] = m_params.game.type;

    // "modmanager".
    j["params"]["modmanager"]["type"] = m_params.modManager.type;
    j["params"]["modmanager"]["mo2instancedir"] = utf16toUTF8(m_params.modManager.mo2InstanceDir.wstring());
    j["params"]["modmanager"]["mo2useloosefileorder"] = m_params.modManager.mo2UseLooseFileOrder;

    // "output".
    j["params"]["output"]["dir"] = utf16toUTF8(m_params.output.dir.wstring());
    j["params"]["output"]["zip"] = m_params.output.zip;
    j["params"]["output"]["pluginlang"] = PGPlugin::stringFromPluginLang(m_params.output.pluginLang);

    // "processing".
    j["params"]["processing"]["multithread"] = m_params.processing.multithread;
    j["params"]["processing"]["devmode"] = m_params.processing.enableModDevMode;
    j["params"]["processing"]["enabledebuglogging"] = m_params.processing.enableDebugLogging;
    j["params"]["processing"]["enabletracelogging"] = m_params.processing.enableTraceLogging;
    j["params"]["processing"]["allowlist"] = utf16VectorToUTF8(m_params.processing.allowList);
    j["params"]["processing"]["blocklist"] = utf16VectorToUTF8(m_params.processing.blockList);
    j["params"]["processing"]["texturemaps"] = nlohmann::json::object();
    for (const auto& [key, value] : m_params.processing.textureMaps)
        j["params"]["processing"]["texturemaps"][utf16toUTF8(key)] = PGEnums::strFromTexType(value);
    j["params"]["processing"]["vanillabsalist"] = utf16VectorToUTF8(m_params.processing.vanillaBSAList);
    j["params"]["processing"]["allowedmodelrecordtypes"] = nlohmann::json::array();
    for (const auto& item : m_params.processing.allowedModelRecordTypes)
        j["params"]["processing"]["allowedmodelrecordtypes"].push_back(PGPlugin::stringFromRecType(item));

    // "prepatcher".
    j["params"]["prepatcher"]["fixmeshlighting"] = m_params.prePatcher.fixMeshLighting;

    // "shaderpatcher".
    j["params"]["shaderpatcher"]["parallax"] = m_params.shaderPatcher.parallax;
    j["params"]["shaderpatcher"]["complexmaterial"] = m_params.shaderPatcher.complexMaterial;
    j["params"]["shaderpatcher"]["truepbr"] = m_params.shaderPatcher.truePBR;

    // "shadertransforms".
    j["params"]["shadertransforms"]["parallaxtocm"] = m_params.shaderTransforms.parallaxToCM;

    // "postpatcher".
    j["params"]["postpatcher"]["disableprepatchedmaterials"] = m_params.postPatcher.disablePrePatchedMaterials;
    j["params"]["postpatcher"]["fixsss"] = m_params.postPatcher.fixSSS;
    j["params"]["postpatcher"]["hairflowmap"] = m_params.postPatcher.hairFlowMap;

    // "globalpatcher".

    return j;
}

bool PGConfig::saveUserConfig()
{
    const auto j = userConfigJSON();

    // Update UserConfig var.
    m_userConfig = j;

    // Write to file.
    try {
        std::filesystem::create_directories(userConfigFile().parent_path());
        FileUtil::saveJSON(userConfigFile(), j, true);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to save user config: {}", e.what());
        return false;
    }

    return true;
}

bool PGConfig::saveModConfig()
{
    // Mods.
    auto* pgmm = PGGlobals::pgmm();
    if (pgmm == nullptr)
        throw std::runtime_error("Mod Manager Directory not set");

    const auto j = pgmm->json();

    // Write to file.
    try {
        std::filesystem::create_directories(modConfigFile().parent_path());
        FileUtil::saveJSON(modConfigFile(), j, true);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to save mod config: {}", e.what());
        return false;
    }

    return true;
}

std::unordered_map<wxString,
                   bool>
PGConfig::ignoredMessagesConfig()
{
    std::unordered_map<wxString, bool> ignoredItems;

    if (!std::filesystem::exists(ignoredMessagesConfigFile()))
        return ignoredItems;

    nlohmann::json j;
    if (!parseJSON(FileUtil::fileBytes(ignoredMessagesConfigFile()), j))
        throw std::runtime_error("Failed to parse ignored messages config JSON");

    if (!j.contains("ignored_messages") || !j["ignored_messages"].is_object())
        throw std::runtime_error("Invalid ignored messages config JSON format");

    for (const auto& item : j["ignored_messages"].items())
        ignoredItems[wxString::FromUTF8(item.key().c_str())] = item.value().get<bool>();

    return ignoredItems;
}

bool PGConfig::saveIgnoredMessagesConfig(const std::unordered_map<wxString,
                                                                  bool>& ignoredItems)
{
    nlohmann::json j;
    j["ignored_messages"] = nlohmann::json::object();

    for (const auto& [key, value] : ignoredItems)
        j["ignored_messages"][std::string(key.ToUTF8().data())] = value;

    // Write to file.
    try {
        std::filesystem::create_directories(ignoredMessagesConfigFile().parent_path());
        FileUtil::saveJSON(ignoredMessagesConfigFile(), j, true);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to save ignored messages config: {}", e.what());
        return false;
    }

    return true;
}
