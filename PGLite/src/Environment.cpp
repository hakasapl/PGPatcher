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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <objbase.h>
#include <optional>
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
 * archive registration (a line that starts with an asterisk; comments and empty lines skipped). Nothing when the file
 * cannot be read.
 */
std::optional<std::vector<std::wstring>> activePluginsTxtLines()
{
    std::vector<std::wstring> names;
    std::ifstream stream(Environment::activePluginsFile());
    if (!stream)
        return std::nullopt;

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
    readMO2DataFolder();
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

void Environment::readMO2DataFolder()
{
    if (m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2)
        m_mo2InstanceDir = resolvedMO2InstanceDir(m_params.modManager.mo2InstanceDir);

    // Only a game launched from MO2 reads through usvfs; elsewhere the data folder is what it is.
    if (PGModManager::mo2DirFromUSVFS().empty())
        return;

    // The instance whose view is what usvfs serves (PGModManager::fileMapInstanceDir): the configured one when it can
    // be the one the game runs under, else the one that launched the game. A configured instance left out is the mod
    // list's alone, which is reported; none means the listing.
    const auto viewInstanceDir = PGModManager::fileMapInstanceDir(m_mo2InstanceDir, activePluginsFile());
    std::error_code ec;
    if (!m_mo2InstanceDir.empty()
        && (viewInstanceDir.empty() || !std::filesystem::equivalent(viewInstanceDir, m_mo2InstanceDir, ec))) {
        Logger::warn(L"The configured Mod Organizer 2 instance {} does not supply the file map (not the instance the "
                     L"game runs under, or not an instance folder); the mod list still comes from it when it can be "
                     L"read",
                     m_mo2InstanceDir.wstring());
    }
    if (viewInstanceDir.empty()) {
        Logger::info("The data folder is listed as the game sees it");
        return;
    }

    const Stopwatch stopwatch;
    try {
        m_mo2DataFolder = std::make_unique<MO2DataFolder>(MO2DataFolder::read(
            viewInstanceDir,
            Settings::dataFolder(),
            { .foldersToMap = PGGlobals::s_foldersToMap, .shouldMultithread = m_params.processing.multithread }));
        Logger::info(L"Read the Mod Organizer 2 instance {} (profile {}): {} files of {} sources in {} ms",
                     viewInstanceDir.wstring(),
                     m_mo2DataFolder->profile(),
                     m_mo2DataFolder->files().size(),
                     m_mo2DataFolder->sources().size(),
                     stopwatch.milliseconds());
    } catch (const std::exception& e) {
        Logger::warn(L"Unable to read the Mod Organizer 2 instance {}, the data folder is listed as the game sees "
                     L"it: {}",
                     viewInstanceDir.wstring(),
                     StringUtil::utf8toUTF16(e.what()));
    }
}

std::filesystem::path Environment::resolvedMO2InstanceDir(const std::filesystem::path& instanceDir)
{
    // A relative path is the game folder's, where the offline program's is its own folder's (section 7); an empty
    // path stays empty (it counts as relative).
    return !instanceDir.empty() && instanceDir.is_relative() ? (Settings::gameFolder() / instanceDir).lexically_normal()
                                                             : instanceDir;
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
            if (m_mo2InstanceDir.empty()) {
                Logger::warn("No Mod Organizer 2 instance is configured, mods are not attributed");
                m_modManager = std::make_unique<PGModManager>(PGModManager::ModManagerType::None);
            } else {
                Logger::info(L"Reading the Mod Organizer 2 instance {}", m_mo2InstanceDir.wstring());
                // The view attributes the files it lists when it is the configured instance's. Another instance's
                // view (the one that launched the game, read when the configured instance could not be) is the file
                // map's alone, and the configured instance gets a view of its own mod folders, which throws when it
                // cannot be read either.
                std::error_code ec;
                if (m_mo2DataFolder
                    && std::filesystem::equivalent(m_mo2DataFolder->instanceDir(), m_mo2InstanceDir, ec)) {
                    m_modManager->populateModFileMapMO2(*m_mo2DataFolder, { });
                } else {
                    const auto view = MO2DataFolder::read(m_mo2InstanceDir,
                                                          Settings::dataFolder(),
                                                          {
                                                              .foldersToMap = PGGlobals::s_foldersToMap,
                                                              .shouldMultithread = m_params.processing.multithread,
                                                              .shouldListDataFolder = false,
                                                          });
                    m_modManager->populateModFileMapMO2(view, { });
                }
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
    // The loose files come from the view of the Mod Organizer 2 instance when there is one (readMO2DataFolder).
    m_directory->setLooseFileSource(m_mo2DataFolder.get());

    Logger::info("Building the file map");
    m_directory->populateFileMap(true, m_params.processing.multithread);
    PGGlobals::setPGD(m_directory.get());

    // The view served its purpose.
    m_directory->setLooseFileSource(nullptr);
    m_mo2DataFolder.reset();

    Logger::info("File map of {} files built in {} ms", m_directory->fileMap().size(), stopwatch.milliseconds());
}

void Environment::switchToEngineReads()
{
    // Every read goes through the engine from here on, what the game itself loads for a path; the archive tables are
    // not needed anymore.
    m_directory->setFileReader([](const std::filesystem::path& relPath) {
        // A path the engine cannot resolve is not there for it, whatever the file map says (an archive of a plugin
        // it did not load); a stream it resolves but cannot read may be readable later (EngineReadError).
        RE::BSTSmartPointer<RE::BSResource::Stream> stream;
        if (!Resources::resolveStream(StringUtil::utf16toWindows1252(relPath.wstring()), stream))
            throw std::runtime_error("The engine cannot resolve " + StringUtil::utf16toUTF8(relPath.wstring()));

        std::vector<std::byte> bytes;
        if (!Resources::readStream(*stream, bytes))
            throw EngineReadError("The engine cannot read " + StringUtil::utf16toUTF8(relPath.wstring()));

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
    // matter, and a built-in plugin with a line of its own is registered once, in its built-in place. Without the
    // file, the loaded plugins stand in, in their load order, once the data is loaded.
    for (const auto& name : builtInPlugins())
        addArchivesOfPlugin(name, candidates);
    if (const auto activeLines = activePluginsTxtLines()) {
        for (const auto& name : *activeLines)
            addArchivesOfPlugin(name, candidates);
    } else {
        auto* const dataHandler = RE::TESDataHandler::GetSingleton();
        const bool areModsLoaded
            = dataHandler && (dataHandler->GetLoadedModCount() || dataHandler->GetLoadedLightModCount());
        if (areModsLoaded) {
            const auto addLoaded = [&candidates](const RE::TESFile& file) {
                addArchivesOfPlugin(StringUtil::windows1252toUTF16(std::string(file.GetFilename())), candidates);
            };
            const auto* const* const mods = dataHandler->GetLoadedMods();
            for (std::uint8_t i = 0; mods && i < dataHandler->GetLoadedModCount(); i++)
                if (mods[i])
                    addLoaded(*mods[i]);

            const auto* const* const lightMods = dataHandler->GetLoadedLightMods();
            for (std::uint16_t i = 0; lightMods && i < dataHandler->GetLoadedLightModCount(); i++)
                if (lightMods[i])
                    addLoaded(*lightMods[i]);
            Logger::warn(L"Unable to read {}, the archives of the plugins are ordered by their load order",
                         activePluginsFile().wstring());
        } else {
            Logger::error(L"Unable to read {} and the plugins are not loaded, the archives of the mods are not mapped",
                          activePluginsFile().wstring());
        }
    }

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

bool Environment::canOrderArchivesEarly() { return activePluginsTxtLines().has_value(); }

void Environment::reportArchivesOfUnloadedPlugins()
{
    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    const auto activeLines = activePluginsTxtLines();
    if (!dataHandler || !activeLines)
        return;

    const auto dataFolder = Settings::dataFolder();
    for (const auto& name : *activeLines) {
        const auto fileName = StringUtil::utf16toWindows1252(name);
        if (dataHandler->LookupLoadedModByName(fileName) || dataHandler->LookupLoadedLightModByName(fileName))
            continue;

        // Only an archive that exists was mapped (engineArchiveOrder()).
        std::vector<std::wstring> archives;
        addArchivesOfPlugin(name, archives);
        const bool hasArchive = std::ranges::any_of(archives, [&dataFolder](const std::wstring& archive) {
            std::error_code ec;
            return std::filesystem::exists(dataFolder / archive, ec);
        });
        if (hasArchive) {
            Logger::warn(L"Plugins.txt lists {} but the engine did not load it, its archives are mapped for nothing",
                         name);
        }
    }
}

std::filesystem::path Environment::activePluginsFile() { return localAppDataGameFolder() / L"Plugins.txt"; }

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
