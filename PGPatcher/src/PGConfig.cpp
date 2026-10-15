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
#include <vector>

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

    // The game location is only user-editable when MO2 does not provide it. A game path from modorganizer.ini is
    // relative to the MO2 folder instead and is resolved by PGModManager::resolveMO2GamePath when it is read.
    const bool isGameDirFromMO2 = params.modManager.type == PGModManager::ModManagerType::ModOrganizer2
        && !PGModManager::gamePathFromInstanceDir(params.modManager.mo2InstanceDir).empty();
    if (!isGameDirFromMO2)
        params.game.dir = resolveExeRelativePath(params.game.dir);
}

auto PGConfig::defaultParams() -> PGParams
{
    auto params = PGParams::defaults();

    // The mod manager and the MO2 instance default to the detected ones (PGModManager::detect): Mod Organizer 2 with
    // the instance that launched PGPatcher when launched from it, Vortex when its deployment manifest is in the data
    // folder, none otherwise. The log is not up this early, so the outcome is not logged.
    const auto gameDir = resolveExeRelativePath(params.game.dir);
    const auto detection = PGModManager::detect(gameDir.empty() ? std::filesystem::path() : gameDir / "Data", false);
    params.modManager.type = detection.type;
    params.modManager.mo2InstanceDir = detection.mo2InstanceDir;
    return params;
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
                PGParams::replaceForwardSlashes(j);
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
    if (j.contains("params"))
        m_params.readJSON(j["params"]);
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

    // Validation messages are shown in the launcher's error dialog, so they are localized like every other GUI string.
    const auto addError = [&errors](const std::string& key) { errors.emplace_back(pgTr(key).utf8_string()); };
    const auto addErrorWithItem = [&errors](const std::string& key, const std::wstring& item) {
        errors.emplace_back(wxString::Format(pgTr(key), wxString(item)).utf8_string());
    };

    // Game.
    if (params.game.dir.empty())
        addError("launcher.validation.gameLocationRequired");

    if (params.modManager.type == PGModManager::ModManagerType::ModOrganizer2 && !params.game.dir.empty()
        && params.game.dir.is_relative()) {
        // MO2 stores the game path relative to its own folder, which PGPatcher finds through the MO2 VFS it was
        // launched from or, for portable instances, the instance folder (see PGModManager::findMO2Dir). Neither worked
        // here: resolveRelativePaths() never resolves a game path that comes from modorganizer.ini against the
        // PGPatcher folder, so a game path that is still relative at this point can only be that unresolved MO2 value.
        addError("launcher.validation.mo2RelativeGamePath");
    } else if (!BethesdaGame::isGamePathValid(params.game.dir, params.game.type)) {
        addError("launcher.validation.gameLocationInvalid");
    }

    // Mod Manager.
    if (params.modManager.type == PGModManager::ModManagerType::ModOrganizer2) {
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

    // The parameters that are not paths of this machine: the shared check, so the PGLite menu refuses the same
    // settings the launcher does.
    for (const auto& error : params.validate())
        if (error.hasItem)
            addErrorWithItem(error.key, error.item);
        else
            addError(error.key);

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
    m_params.writeJSON(j["params"]);

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
    if (!pgmm)
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
