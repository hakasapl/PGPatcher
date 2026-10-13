#include "PCH.h"

#include "Environment.hpp"

#include "ClassificationCache.hpp"
#include "PGGlobals.hpp"
#include "PGPatcher.hpp"
#include "PGRunObserver.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <nlohmann/json.hpp>

#include <boost/algorithm/string.hpp>

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <objbase.h>
#include <shared_mutex>
#include <shlobj.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/// @brief The top level folders of the data folder the runtime maps: the textures, the TruePBR configs and the meshes
/// the mesh reading pass classifies the textures from.
const std::unordered_set<std::filesystem::path> foldersToMap = { L"textures", L"pbrnifpatcher", L"meshes" };

constexpr const char* archiveListSetting = "sResourceArchiveList:Archive";
constexpr const char* archiveList2Setting = "sResourceArchiveList2:Archive";
constexpr const wchar_t* updateArchive = L"Update.bsa";

class Stopwatch {
public:
    Stopwatch() = default;

    [[nodiscard]] long long milliseconds() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start)
            .count();
    }

private:
    std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

void addArchivesOfList(const char* settingName,
                       std::vector<std::wstring>& archives)
{
    auto* const collection = RE::INISettingCollection::GetSingleton();
    auto* const setting = collection ? collection->GetSetting(settingName) : nullptr;
    const char* const value = setting ? setting->GetString() : nullptr;
    if (!value)
        return;

    std::vector<std::string> names;
    boost::split(names, std::string(value), boost::is_any_of(","));
    for (auto& name : names) {
        boost::trim(name);
        if (!name.empty())
            archives.push_back(StringUtil::windows1252toUTF16(name));
    }
}

/// @brief The archives the engine registers for a plugin file name: <name>.bsa and <name> - Textures.bsa.
void addArchivesOfPlugin(const std::wstring& fileName,
                         std::vector<std::wstring>& archives)
{
    const auto stem = std::filesystem::path(fileName).stem().wstring();
    archives.push_back(stem + L".bsa");
    archives.push_back(stem + L" - Textures.bsa");
}

/// @brief The folder of the game's Plugins.txt under the local application data, named as the engine names it for the
/// store the game came from.
std::filesystem::path localAppDataGameFolder()
{
    wchar_t* buffer = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &buffer)) || !buffer)
        return { };

    std::filesystem::path folder(buffer);
    CoTaskMemFree(buffer);

    std::error_code ec;
    const auto gameFolder = Settings::gameFolder();
    if (std::filesystem::exists(gameFolder / L"steam_api64.dll", ec))
        return folder / L"Skyrim Special Edition";
    if (std::filesystem::exists(gameFolder / L"EOSSDK-Win64-Shipping.dll", ec))
        return folder / L"Skyrim Special Edition EPIC";

    return folder / L"Skyrim Special Edition GOG";
}

/**
 * @brief The plugin file names of the active lines of Plugins.txt in file order, as the engine reads them for its
 * archive registration (a line that starts with an asterisk; comments and empty lines skipped). Empty when the file
 * cannot be read.
 */
std::vector<std::wstring> activePluginsTxtLines()
{
    std::vector<std::wstring> names;
    const auto file = localAppDataGameFolder() / L"Plugins.txt";
    std::ifstream stream(file);
    if (!stream) {
        Logger::warn(L"Unable to read {}, the archives of the plugins are ordered by their load order", file.wstring());
        return names;
    }

    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (line.size() < 2 || line.front() != '*')
            continue;

        names.push_back(StringUtil::windows1252toUTF16(line.substr(1)));
    }

    return names;
}

}

Environment::Environment(PGParams params,
                         Settings::Ini ini)
    : m_params(std::move(params))
    , m_ini(ini)
{
}

Environment::~Environment()
{
    if (PGGlobals::isPGDSet() && PGGlobals::pgd() == m_directory.get())
        PGGlobals::setPGD(nullptr);
    if (PGGlobals::isPGMMSet() && PGGlobals::pgmm() == m_modManager.get())
        PGGlobals::setPGMM(nullptr);
}

void Environment::initialize(ClassificationCache& cache,
                             const std::function<void(const char*)>& reportStage,
                             const ProgressCallback& reportProgress)
{
    const Stopwatch stopwatch;
    const auto stage = [&reportStage](const char* name) {
        if (reportStage)
            reportStage(name);
    };

    stage("progress.steps.initModManager");
    initializeModManager();
    stage("progress.steps.populatingFileMap");
    initializeDirectory();
    stage("pglite.stages.readingMeshes");
    classifyTextures(cache, reportProgress);

    if (m_params.shaderPatcher.isTruePBREnabled) {
        stage("pglite.stages.loadingTruePBRConfigs");
        Logger::info("Loading {} TruePBR configs", m_directory->pbrJSONs().size());
        PatcherMeshShaderTruePBR::loadStatics(m_directory->pbrJSONs(), m_params.processing.multithread);
    }

    switchToEngineReads();

    if (m_modManager) {
        // Shader attribution of every mod is complete now, so the mod list and the priorities are the offline ones.
        stage("pglite.stages.orderingMods");
        m_modManager->updateStateFromModlist(m_params.modManager.shouldUseMO2LooseFileOrder);
    }

    stage("progress.steps.initPatchers");
    registerPatchers(m_params, m_ini);

    Logger::info("Runtime environment ready in {} ms", stopwatch.milliseconds());
}

void Environment::initializeModManager()
{
    m_modManager = std::make_unique<PGModManager>(m_params.modManager.type);

    nlohmann::json modRules;
    if (Settings::readModRules(modRules)) {
        try {
            m_modManager->loadJSON(modRules);
        } catch (const std::exception& e) {
            Logger::error("Unable to read modrules.json: {}", e.what());
        }
    }

    try {
        if (m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2) {
            // A relative path is the game folder's, where the offline program's is its own folder's (section 7); an
            // empty path stays empty (it counts as relative).
            const auto& instanceDir = m_params.modManager.mo2InstanceDir;
            m_mo2InstanceDir = !instanceDir.empty() && instanceDir.is_relative()
                ? (Settings::gameFolder() / instanceDir).lexically_normal()
                : instanceDir;
            if (m_mo2InstanceDir.empty()) {
                Logger::warn("No Mod Organizer 2 instance is configured, mods are not attributed");
            } else {
                Logger::info(L"Reading the Mod Organizer 2 instance {}", m_mo2InstanceDir.wstring());
                m_modManager->populateModFileMapMO2(m_mo2InstanceDir, { });
            }
        } else if (m_params.modManager.type == PGModManager::ModManagerType::Vortex) {
            Logger::info("Reading the Vortex deployment");
            m_modManager->populateModFileMapVortex(Settings::dataFolder());
        }
    } catch (const std::exception& e) {
        Logger::error("Unable to read the mod manager data, mods are not attributed: {}", e.what());
        m_modManager = std::make_unique<PGModManager>(PGModManager::ModManagerType::None);
    }

    // The mesh reading pass marks the mods that own a mesh, as offline.
    PGGlobals::setPGMM(m_modManager.get());
}

void Environment::initializeDirectory()
{
    const Stopwatch stopwatch;

    // The directory reads as offline until switchToEngineReads(): loose files from the data folder (the mod manager's
    // virtual file system is process wide, so they are the files the game sees) and archived files from its own
    // archive tables, built from the engine's archive set. The engine's resolver takes the registry lock exclusively
    // for every read, and the mesh reading pass reads every mesh on the task pool, which would starve the main thread.
    m_directory = std::make_unique<PGDirectory>(Settings::dataFolder(), std::filesystem::path());
    m_directory->setFoldersToMap(foldersToMap);
    m_directory->setShouldSkipHiddenFiles(false);
    m_directory->setArchiveLoadOrder(engineArchiveOrder());

    Logger::info("Building the file map");
    m_directory->populateFileMap(true, m_params.processing.multithread);
    PGGlobals::setPGD(m_directory.get());

    Logger::info("File map of {} files built in {} ms", m_directory->fileMap().size(), stopwatch.milliseconds());
}

void Environment::switchToEngineReads()
{
    // Every read goes through the engine from here on, what the game itself loads for a path; the archive tables are
    // not needed anymore.
    m_directory->setFileReader([](const std::filesystem::path& relPath) {
        std::vector<std::byte> bytes;
        if (!Resources::readFile(StringUtil::utf16toWindows1252(relPath.wstring()), bytes))
            throw std::runtime_error("The engine cannot read " + StringUtil::utf16toUTF8(relPath.wstring()));

        return bytes;
    });
    m_directory->dropArchiveContents();
}

void Environment::classifyTextures(ClassificationCache& cache,
                                   const ProgressCallback& reportProgress)
{
    const Stopwatch stopwatch;
    const auto cmBefore = cache.cmCount();
    const auto meshesBefore = cache.meshCount();

    // The offline mapping pass: every mesh of the load order casts the texture votes of its shapes (from the cache when
    // the mesh did not change), then the textures are classified and the environment masks go through the complex
    // material classification. The uses of the meshes come from the loaded game (PGPluginRuntime).
    Logger::info("Reading the meshes and classifying the textures");
    m_directory->mapFiles(m_params.processing.blockList,
                          m_params.processing.allowList,
                          m_params.processing.textureMaps,
                          m_params.processing.vanillaBSAList,
                          m_params.processing.multithread,
                          reportProgress);
    m_directory->waitForMeshMapping();
    m_directory->waitForCMClassification();
    cache.save();

    Logger::info("Read {} meshes ({} added to the cache) and classified {} textures ({} complex material "
                 "classifications added to the cache) in {} ms",
                 m_directory->meshes().size(),
                 cache.meshCount() - meshesBefore,
                 m_directory->textures().size(),
                 cache.cmCount() - cmBefore,
                 stopwatch.milliseconds());
}

std::vector<std::wstring> Environment::engineArchiveOrder()
{
    std::vector<std::wstring> candidates;
    addArchivesOfList(archiveListSetting, candidates);
    addArchivesOfList(archiveList2Setting, candidates);
    candidates.emplace_back(updateArchive);

    // The engine registers the archives of its built-in plugin list (the masters and the Creation Club content), then
    // those of the active lines of Plugins.txt in file order. The loaded plugins without a line are the built-in ones;
    // their archives share no file, so their order among themselves does not matter.
    const auto activeLines = activePluginsTxtLines();
    std::unordered_set<std::wstring> activeLower;
    for (const auto& name : activeLines)
        activeLower.insert(StringUtil::toLowerASCIIFast(name));
    if (auto* const dataHandler = RE::TESDataHandler::GetSingleton()) {
        const auto addBuiltIn = [&candidates, &activeLower](const RE::TESFile& file) {
            const auto fileName = StringUtil::windows1252toUTF16(std::string(file.GetFilename()));
            if (!activeLower.contains(StringUtil::toLowerASCIIFast(fileName)))
                addArchivesOfPlugin(fileName, candidates);
        };
        const auto* const* const mods = dataHandler->GetLoadedMods();
        for (std::uint8_t i = 0; mods && i < dataHandler->GetLoadedModCount(); i++)
            if (mods[i])
                addBuiltIn(*mods[i]);

        const auto* const* const lightMods = dataHandler->GetLoadedLightMods();
        for (std::uint16_t i = 0; lightMods && i < dataHandler->GetLoadedLightModCount(); i++)
            if (lightMods[i])
                addBuiltIn(*lightMods[i]);
    }
    for (const auto& name : activeLines)
        addArchivesOfPlugin(name, candidates);

    // The engine registers an archive once, when it exists.
    std::vector<std::wstring> archives;
    std::unordered_set<std::wstring> seen;
    const auto dataFolder = Settings::dataFolder();
    for (const auto& candidate : candidates) {
        if (!seen.insert(StringUtil::toLowerASCIIFast(candidate)).second)
            continue;

        std::error_code ec;
        if (std::filesystem::exists(dataFolder / candidate, ec))
            archives.push_back(candidate);
    }

    Logger::debug("The engine loads {} archives", archives.size());
    return archives;
}

void Environment::registerPatchers(const PGParams& params,
                                   const Settings::Ini& ini)
{
    PatcherUtil::PatcherMeshSet meshPatchers;

    // The stock marker reverts pre-patched shapes to stock, so it has to run before every other patcher.
    meshPatchers.prePatchers.emplace_back(PatcherMeshPreStockMarker::factory());
    if (params.prePatcher.isFixMeshLightingEnabled)
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixMeshLighting::factory());

    const bool hasShaderPatcher = params.shaderPatcher.isParallaxEnabled
        || params.shaderPatcher.isComplexMaterialEnabled || params.shaderPatcher.isTruePBREnabled;
    if (hasShaderPatcher)
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::factory());

    meshPatchers.shaderPatchers.emplace(PatcherMeshShaderDefault::shaderType(), PatcherMeshShaderDefault::factory());
    if (params.shaderPatcher.isParallaxEnabled) {
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderVanillaParallax::shaderType(),
                                            PatcherMeshShaderVanillaParallax::factory());
    }
    if (params.shaderPatcher.isComplexMaterialEnabled) {
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderComplexMaterial::shaderType(),
                                            PatcherMeshShaderComplexMaterial::factory());
        PatcherMeshShaderComplexMaterial::loadOptions(ini.shouldDisableDynamicCubemap);
    }
    if (params.shaderPatcher.isTruePBREnabled) {
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderTruePBR::shaderType(),
                                            PatcherMeshShaderTruePBR::factory());
        PatcherMeshShaderTruePBR::loadOptions(true, params.processing.enableModDevMode);
    }

    // The transform and Fix SSS register textures with their hooks, and the runtime generates those the first time a
    // patched mesh needs them (TextureGenerator). A hook whose shader cannot be created leaves its patcher off, where
    // the offline program refuses to run (DESIGN.md section 7).
    if (params.shaderTransforms.isParallaxToCMEnabled) {
        if (!params.shaderPatcher.isParallaxEnabled || !params.shaderPatcher.isComplexMaterialEnabled) {
            // The menu and the offline launcher refuse this combination (PGParams::validate); an edited settings.json
            // gets the patchers it names and no transform, which would fail on every parallax match.
            Logger::error("The parallax to complex material transform needs the parallax and the complex material "
                          "patchers, the transform is off");
        } else if (PatcherTextureHookConvertToCM::initShader()) {
            meshPatchers.shaderTransformPatchers[PatcherMeshShaderTransformParallaxToCM::fromShader()]
                = { PatcherMeshShaderTransformParallaxToCM::toShader(),
                    PatcherMeshShaderTransformParallaxToCM::factory() };
            PatcherMeshShaderTransformParallaxToCM::loadOptions(!ini.shouldForceAlwaysCM);
        } else {
            Logger::error("The parallax to complex material shader could not be created, the transform is off");
        }
    }

    if (params.postPatcher.disablePrePatchedMaterials)
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::factory());
    if (params.postPatcher.isFixSSSEnabled) {
        if (PatcherTextureHookFixSSS::initShader())
            meshPatchers.postPatchers.emplace_back(PatcherMeshPostFixSSS::factory());
        else
            Logger::error("The subsurface scattering shader could not be created, Fix SSS is off");
    }
    if (params.postPatcher.isHairFlowMapEnabled)
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::factory());

    PGPatcher::loadPatchers(meshPatchers, { });
}

}
