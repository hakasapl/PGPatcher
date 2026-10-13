#include "PCH.h"

#include "LightPlacerBridge.hpp"

#include "Plan.hpp"
#include "Settings.hpp"
#include "handlers/HandlerLightPlacerTracker.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr const wchar_t* lightPlacerModule = L"po3_LightPlacer.dll";
constexpr const wchar_t* configFolderName = L"LightPlacer";
constexpr const wchar_t* configFileName = L"PGLite_Variants.json";
constexpr std::wstring_view meshesPrefix = L"meshes\\";

/// @brief Light Placer's console command that reads its configs again and rebuilds its tables (its src/Debug.cpp).
constexpr std::string_view reloadCommand = "ReloadLP";

/// @brief The model string a record holds for a mesh path: without the meshes\ prefix, as setModelPath() writes it
/// and as Light Placer compares it (case insensitively, otherwise literally). Empty for a path with a character outside
/// ASCII: the configs are UTF-8 JSON and the engine's strings are not, so no config entry can match such a model.
std::string recordModelString(const std::filesystem::path& meshPath)
{
    auto rest = meshPath.wstring();
    if (rest.starts_with(meshesPrefix))
        rest.erase(0, meshesPrefix.size());

    if (!StringUtil::containsOnlyAscii(rest))
        return { };

    return StringUtil::utf16toASCII(rest);
}

}

bool LightPlacerBridge::isLightPlacerLoaded() { return GetModuleHandleW(lightPlacerModule) != nullptr; }

std::filesystem::path LightPlacerBridge::configFile()
{
    return Settings::dataFolder() / configFolderName / configFileName;
}

void LightPlacerBridge::update(const Plan& plan)
{
    if (!isLightPlacerLoaded())
        return;

    try {
        // The variant model strings of every original model string of the plan, keyed as the offline handler keys
        // the entries of the configs (HandlerLightPlacerTracker::modelKey).
        std::unordered_map<std::filesystem::path, std::vector<std::string>> variantsOf;
        std::size_t unlisted = 0;
        for (const auto& family : plan.families()) {
            for (const auto& member : family->members) {
                if (!member.variantNumber)
                    continue;

                for (std::size_t i = 0; i < family->basePaths.size() && i < member.paths.size(); i++) {
                    const auto original = recordModelString(family->basePaths.at(i));
                    const auto variant = recordModelString(member.paths.at(i));
                    if (original.empty() || variant.empty()) {
                        unlisted++;
                        continue;
                    }

                    variantsOf[HandlerLightPlacerTracker::modelKey(original)].push_back(variant);
                }
            }
        }
        if (unlisted)
            Logger::debug("{} variants of models with characters outside ASCII are not listed for Light Placer",
                          unlisted);

        const auto file = configFile();
        const auto folder = file.parent_path();
        nlohmann::json entries = nlohmann::json::array();
        std::size_t configsRead = 0;
        std::error_code ec;
        if (!variantsOf.empty() && std::filesystem::is_directory(folder, ec)) {
            // Light Placer's own walk: every file with the .json extension, compared as is, under the folder.
            for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
                if (entry.is_directory() || entry.path().extension() != ".json"
                    || entry.path().filename() == configFileName) {
                    continue;
                }

                nlohmann::json config;
                if (!HandlerLightPlacerTracker::parseLightPlacerJSON(entry.path(), config))
                    continue; // Light Placer does not load it either

                configsRead++;
                for (const auto& block : config) {
                    if (!block.is_object() || !block.contains("models") || !block["models"].is_array())
                        continue;

                    std::vector<std::string> variants;
                    for (const auto& model : block["models"]) {
                        if (!model.is_string())
                            continue;

                        const auto it = variantsOf.find(HandlerLightPlacerTracker::modelKey(model.get<std::string>()));
                        if (it == variantsOf.end())
                            continue;

                        for (const auto& variant : it->second)
                            if (std::ranges::find(variants, variant) == variants.end())
                                variants.push_back(variant);
                    }

                    if (variants.empty())
                        continue;

                    // The entry as it is, its lights included, for the variants instead of the originals.
                    auto copy = block;
                    copy["models"] = variants;
                    entries.push_back(std::move(copy));
                }
            }
        }

        if (entries.empty()) {
            // Light Placer holds the entries of an earlier config until it reads its configs again.
            if (std::filesystem::remove(file, ec)) {
                Logger::info(L"Removed {}, no Light Placer entry lists a model with variants", file.wstring());
                requestReload();
            }

            return;
        }

        // Serialized first, then written whole or not at all: Light Placer reads the file at its plugin load.
        const auto text = entries.dump(2);
        if (!Settings::writeFileAtomically(file, text.data(), text.size())) {
            Logger::error(L"Unable to write the Light Placer config of the variants {}", file.wstring());
            return;
        }

        Logger::info(L"Wrote {} Light Placer entries for the variant paths to {} ({} configs read)",
                     entries.size(),
                     file.wstring(),
                     configsRead);
        requestReload();
    } catch (const std::exception& e) {
        Logger::error("The Light Placer config of the variants could not be written: {}", e.what());
    }
}

void LightPlacerBridge::requestReload()
{
    // Light Placer has no API; its console command reads every config again and rebuilds its tables. The task runs on
    // the main thread once the current step is done: after the data load (every plugin's data-load handler ran by
    // then, Light Placer's among them) or after Apply.
    SKSE::GetTaskInterface()->AddTask([] {
        auto* const factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
        auto* const script = factory ? factory->Create() : nullptr;
        if (!script) {
            Logger::error("Unable to ask Light Placer to reload its configs: no script form");
            return;
        }

        script->SetCommand(reloadCommand);
        script->CompileAndRun(nullptr);
        delete script; // NOLINT(cppcoreguidelines-owning-memory)
        Logger::info("Asked Light Placer to reload its configs ({})", reloadCommand);
    });
}

}
