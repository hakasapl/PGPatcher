#include "PCH.h"

#include "Settings.hpp"

#include "Logging.hpp"
#include "PGParams.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"

#include <SimpleIni.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr const char* iniSection = "General";

/// @brief Gives a settings document without a "params" object the given parameters, so a file PGLite writes before
/// its first Apply reads the same in the offline program, which takes any file for a complete one.
void ensureParams(nlohmann::json& settings,
                  const PGParams& params)
{
    if (!settings.contains("params"))
        params.writeJSON(settings["params"]);
}

}

std::filesystem::path Settings::gameFolder()
{
    static const std::filesystem::path folder = [] {
        std::vector<wchar_t> buffer(MAX_PATH);
        while (true) {
            const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (!length)
                return std::filesystem::current_path();

            if (length < buffer.size())
                return std::filesystem::path(buffer.data(), buffer.data() + length).parent_path();

            buffer.resize(buffer.size() * 2);
        }
    }();

    return folder;
}

std::filesystem::path Settings::dataFolder() { return gameFolder() / L"Data"; }

std::filesystem::path Settings::pluginsFolder() { return dataFolder() / L"SKSE" / L"Plugins"; }

std::filesystem::path Settings::configFolder()
{
    const auto folder = pluginsFolder() / L"PGLite";

    std::error_code ec;
    std::filesystem::create_directories(folder, ec);

    return folder;
}

std::filesystem::path Settings::iniFile() { return pluginsFolder() / L"PGLite.ini"; }

std::filesystem::path Settings::settingsFile() { return configFolder() / L"settings.json"; }

bool Settings::hasSettingsFile()
{
    std::error_code ec;
    return std::filesystem::exists(settingsFile(), ec);
}

std::filesystem::path Settings::modRulesFile() { return configFolder() / L"modrules.json"; }

std::filesystem::path Settings::classificationCacheFile() { return configFolder() / L"classification.bin"; }

std::filesystem::path Settings::translationsFolder() { return configFolder() / L"translations"; }

std::filesystem::path Settings::textureCacheFolder() { return configFolder() / L"cache"; }

Settings::Ini Settings::readIni()
{
    Ini ini;

    CSimpleIniA file;
    file.SetUnicode();
    if (file.LoadFile(iniFile().c_str()) < 0)
        return ini;

    ini.menuKey = static_cast<unsigned>(file.GetLongValue(iniSection, "iMenuKey", static_cast<long>(defaultMenuKey)));
    ini.logLevel = Logging::levelFromString(file.GetValue(iniSection, "sLogLevel", "info"));
    ini.shouldExcludeFacegens = file.GetBoolValue(iniSection, "bExcludeFacegens", false);
    ini.shouldDisableDynamicCubemap = file.GetBoolValue(iniSection, "bDisableDynamicCubemap", false);
    ini.shouldForceAlwaysCM = file.GetBoolValue(iniSection, "bForceAlwaysCM", false);
    ini.shouldShowLoadingMessage = file.GetBoolValue(iniSection, "bShowLoadingMessage", true);
    ini.shouldShowWelcomeMessage = file.GetBoolValue(iniSection, "bShowWelcomeMessage", true);

    return ini;
}

PGParams Settings::readParams()
{
    // As the offline program reads its user config: the file holds every parameter with its full lists, and the
    // defaults apply only when there is no file.
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.contains("params"))
        return PGParams::defaults();

    try {
        PGParams::replaceForwardSlashes(settings);
        PGParams params;
        params.readJSON(settings["params"]);
        return params;
    } catch (const std::exception& e) {
        Logger::error("Unable to read settings.json, the defaults are used: {}", e.what());
        return PGParams::defaults();
    }
}

bool Settings::writeParams(const PGParams& params)
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.is_object())
        settings = nlohmann::json::object();

    try {
        params.writeJSON(settings["params"]);
        FileUtil::saveJSON(settingsFile(), settings, true);
    } catch (const std::exception& e) {
        Logger::error("Unable to write settings.json: {}", e.what());
        return false;
    }

    return true;
}

bool Settings::readIsEnabled()
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.contains("pglite") || !settings["pglite"].is_object())
        return true;

    const auto& pglite = settings["pglite"];
    return !pglite.contains("enabled") || !pglite["enabled"].is_boolean() || pglite["enabled"].get<bool>();
}

bool Settings::writeIsEnabled(bool isEnabled)
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.is_object())
        settings = nlohmann::json::object();

    try {
        ensureParams(settings, PGParams::defaults());
        settings["pglite"]["enabled"] = isEnabled;
        FileUtil::saveJSON(settingsFile(), settings, true);
    } catch (const std::exception& e) {
        Logger::error("Unable to write settings.json: {}", e.what());
        return false;
    }

    return true;
}

std::string Settings::readLanguage()
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.contains("pglite") || !settings["pglite"].is_object())
        return { };

    const auto& pglite = settings["pglite"];
    if (!pglite.contains("language") || !pglite["language"].is_string())
        return { };

    return pglite["language"].get<std::string>();
}

bool Settings::writeLanguage(const std::string& language)
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.is_object())
        settings = nlohmann::json::object();

    try {
        ensureParams(settings, PGParams::defaults());
        settings["pglite"]["language"] = language;
        FileUtil::saveJSON(settingsFile(), settings, true);
    } catch (const std::exception& e) {
        Logger::error("Unable to write settings.json: {}", e.what());
        return false;
    }

    return true;
}

bool Settings::readDidOpenMenu()
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.contains("pglite") || !settings["pglite"].is_object())
        return false;

    const auto& pglite = settings["pglite"];
    return pglite.contains("didOpenMenu") && pglite["didOpenMenu"].is_boolean() && pglite["didOpenMenu"].get<bool>();
}

bool Settings::writeDidOpenMenu(const PGParams& params)
{
    nlohmann::json settings;
    if (!FileUtil::getJSON(settingsFile(), settings) || !settings.is_object())
        settings = nlohmann::json::object();

    try {
        ensureParams(settings, params);
        settings["pglite"]["didOpenMenu"] = true;
        FileUtil::saveJSON(settingsFile(), settings, true);
    } catch (const std::exception& e) {
        Logger::error("Unable to write settings.json: {}", e.what());
        return false;
    }

    return true;
}

bool Settings::readModRules(nlohmann::json& rules) { return FileUtil::getJSON(modRulesFile(), rules); }

bool Settings::writeModRules(const nlohmann::json& rules)
{
    try {
        FileUtil::saveJSON(modRulesFile(), rules, true);
    } catch (const std::exception& e) {
        Logger::error("Unable to write modrules.json: {}", e.what());
        return false;
    }

    return true;
}

bool Settings::writeFileAtomically(const std::filesystem::path& file,
                                   const void* data,
                                   std::size_t size)
{
    auto temporary = file;
    temporary += L".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;

        stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        stream.close(); // the last buffered block is written by the close, so the check comes after it
        if (!stream) {
            std::error_code removeError;
            std::filesystem::remove(temporary, removeError);
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::rename(temporary, file, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }

    return true;
}

}
