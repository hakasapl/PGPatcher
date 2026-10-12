#include "PCH.h"

#include "Environment.hpp"

#include "ClassificationCache.hpp"
#include "PGGlobals.hpp"
#include "PGPatcher.hpp"
#include "PGRunObserver.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Uses.hpp"
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
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/// @brief The texture of the package, which marks no mod as a texture mod.
constexpr std::wstring_view dynamicCubemapTexture = L"textures\\cubemaps\\dynamic1pxcubemap_black.dds";

/// @brief The top level folders of the data folder the runtime maps: the textures, the TruePBR configs and the meshes
/// the mesh reading pass classifies the textures from.
const std::unordered_set<std::filesystem::path> foldersToMap = { L"textures", L"pbrnifpatcher", L"meshes" };

constexpr const char* archiveListSetting = "sResourceArchiveList:Archive";
constexpr const char* archiveList2Setting = "sResourceArchiveList2:Archive";
constexpr const wchar_t* updateArchive = L"Update.bsa";

/// @brief MO2's global instance registry value and folder.

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

void addArchivesOfPlugin(const RE::TESFile& file,
                         std::vector<std::wstring>& archives)
{
    const auto fileName = StringUtil::windows1252toUTF16(std::string(file.GetFilename()));
    const auto stem = std::filesystem::path(fileName).stem().wstring();
    archives.push_back(stem + L".bsa");
    archives.push_back(stem + L" - Textures.bsa");
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
            m_mo2InstanceDir = m_params.modManager.mo2InstanceDir;
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

void Environment::attributeMods(PGModManager& modManager,
                                PGDirectory& directory)
{
    // The shader a classified texture gives its mod, as PGDirectory::updateTextureType does during classification.
    for (const auto& [path, file] : directory.fileMap()) {
        const auto& native = path.native();
        if (!native.starts_with(L"textures\\") || !native.ends_with(L".dds"))
            continue;

        // The dynamic cubemap PGLite ships gives its own mod no shader, so that mod stays out of the list.
        if (native == dynamicCubemapTexture)
            continue;

        auto shader = PGEnums::ShapeShader::None;
        switch (directory.textureType(path)) {
        case PGEnums::TextureType::Height:
            shader = PGEnums::ShapeShader::VanillaParallax;
            break;
        case PGEnums::TextureType::ComplexMaterial:
            shader = PGEnums::ShapeShader::ComplexMaterial;
            break;
        default:
            break;
        }
        modManager.addShaderToModByFile(path, shader);
    }
    for (const auto& json : directory.pbrJSONs())
        modManager.addShaderToModByFile(json, PGEnums::ShapeShader::TruePBR);

    // A mod has meshes when it owns a mesh the mesh reading pass read, as PGDirectory::mapTexturesFromNIF marks it.
    for (const auto& [path, cache] : directory.meshes()) {
        const auto mod = modManager.modByFileSmart(path);
        if (!mod)
            continue;

        const std::unique_lock lock(mod->mutex);
        mod->hasMeshes = true;
    }
}

std::vector<std::wstring> Environment::engineArchiveOrder()
{
    std::vector<std::wstring> candidates;
    addArchivesOfList(archiveListSetting, candidates);
    addArchivesOfList(archiveList2Setting, candidates);
    candidates.emplace_back(updateArchive);

    if (auto* const dataHandler = RE::TESDataHandler::GetSingleton()) {
        const auto* const* const mods = dataHandler->GetLoadedMods();
        for (std::uint8_t i = 0; mods && i < dataHandler->GetLoadedModCount(); i++)
            if (mods[i])
                addArchivesOfPlugin(*mods[i], candidates);

        const auto* const* const lightMods = dataHandler->GetLoadedLightMods();
        for (std::uint16_t i = 0; lightMods && i < dataHandler->GetLoadedLightModCount(); i++)
            if (lightMods[i])
                addArchivesOfPlugin(*lightMods[i], candidates);
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
    // patched mesh needs them (TextureGenerator). A hook whose shader cannot be created leaves its patcher off.
    if (params.shaderTransforms.isParallaxToCMEnabled) {
        if (PatcherTextureHookConvertToCM::initShader()) {
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
