#include "PCH.h"

#include "Settings.hpp"

#include "Logging.hpp"
#include "PGParams.hpp"
#include "util/FileUtil.hpp"
#include "util/Logger.hpp"

#include <SimpleIni.h>
#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr const char* iniSection = "General";
constexpr const char* debugSection = "Debug";

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

std::filesystem::path Settings::modRulesFile() { return configFolder() / L"modrules.json"; }

std::filesystem::path Settings::cmCacheFile() { return configFolder() / L"cmcache.bin"; }

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
    ini.loadSaveAtStart = file.GetValue(debugSection, "sLoadSaveAtStart", "");
    ini.shouldOpenMenuAtStart = file.GetBoolValue(debugSection, "bOpenMenuAtStart", false);

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

}
