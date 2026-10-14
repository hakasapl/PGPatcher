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

    if (REL::Module::IsVR())
        return folder / L"Skyrim VR";

    std::error_code ec;
    const auto gameFolder = Settings::gameFolder();
    if (std::filesystem::exists(gameFolder / L"steam_api64.dll", ec))
        return folder / L"Skyrim Special Edition";
    if (std::filesystem::exists(gameFolder / L"EOSSDK-Win64-Shipping.dll", ec))
        return folder / L"Skyrim Special Edition EPIC";

    return folder / L"Skyrim Special Edition GOG";
}

/**
 * @brief The plugin file names the engine loads without a line of Plugins.txt: the masters of the game and the
 * Creation Club content of Skyrim.ccc, read from the files so that the archives are known before the data is loaded.
 */
std::vector<std::wstring> builtInPlugins()
{
    std::vector<std::wstring> names
        = { L"Skyrim.esm", L"Update.esm", L"Dawnguard.esm", L"HearthFires.esm", L"Dragonborn.esm" };
    if (REL::Module::IsVR())
        names.emplace_back(L"SkyrimVR.esm");

    std::ifstream stream(Settings::gameFolder() / L"Skyrim.ccc");
    std::string line;
    while (stream && std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        if (!line.empty() && line.front() != '#')
            names.push_back(StringUtil::windows1252toUTF16(line));
    }

    return names;
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
                         Settings::Ini ini,
                         std::unordered_set<std::filesystem::path> excludedFiles)
    : m_params(std::move(params))
    , m_ini(ini)
    , m_excludedFiles(std::move(excludedFiles))
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
    // The time since the previous lap, for the breakdown of the summary line.
    long long lastLap = 0;
    const auto lap = [&stopwatch, &lastLap] {
        const auto now = stopwatch.milliseconds();
        const auto elapsed = now - lastLap;
        lastLap = now;
        return elapsed;
    };

    stage("progress.steps.initModManager");
    initializeModManager();
    const auto modManagerMs = lap();
    stage("progress.steps.populatingFileMap");
    initializeDirectory();
    const auto fileMapMs = lap();
    stage("pglite.stages.readingMeshes");
    classifyTextures(cache, reportProgress);
    const auto meshesMs = lap();

    long long pbrConfigsMs = 0;
    if (m_params.shaderPatcher.isTruePBREnabled) {
        stage("pglite.stages.loadingTruePBRConfigs");
        Logger::info("Loading {} TruePBR configs", m_directory->pbrJSONs().size());
        PatcherMeshShaderTruePBR::loadStatics(m_directory->pbrJSONs(), m_params.processing.multithread);
        pbrConfigsMs = lap();
    }

    switchToEngineReads();

    long long modOrderMs = 0;
    if (m_modManager) {
        // Shader attribution of every mod is complete now, so the mod list and the priorities are the offline ones.
        stage("pglite.stages.orderingMods");
        m_modManager->updateStateFromModlist(m_params.modManager.shouldUseMO2LooseFileOrder);
        modOrderMs = lap();
    }

    stage("progress.steps.initPatchers");
    registerPatchers(m_params, m_ini);
    const auto patchersMs = lap();

    Logger::info("Runtime environment ready in {} ms: mod manager {} ms, file map {} ms, meshes {} ms, PBR configs {} "
                 "ms, mod order {} ms, patchers {} ms",
                 stopwatch.milliseconds(),
                 modManagerMs,
                 fileMapMs,
                 meshesMs,
                 pbrConfigsMs,
                 modOrderMs,
                 patchersMs);
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
    m_directory->setExcludedFiles(m_excludedFiles);

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
    // those of the active lines of Plugins.txt in file order. Both lists come from the files, so the order is known
    // before the data is loaded; the built-in archives share no file, so their order among themselves does not
    // matter, and a built-in plugin with a line of its own is registered once, in its built-in place.
    for (const auto& name : builtInPlugins())
        addArchivesOfPlugin(name, candidates);
    for (const auto& name : activePluginsTxtLines())
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
