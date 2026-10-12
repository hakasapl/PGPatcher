#include "PCH.h"

#include "Environment.hpp"

#include "CMCache.hpp"
#include "PGGlobals.hpp"
#include "PGPatcher.hpp"
#include "PGRunObserver.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Uses.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
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
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

namespace {

/// @brief The top level folders of the data folder the runtime maps: textures and the TruePBR configs. Meshes are
/// mapped too and pruned to the used ones right after, so archived meshes can be attributed to their mod.
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

void Environment::initialize(const Uses& uses,
                             CMCache& cmCache,
                             const std::function<void(const char*)>& reportStage)
{
    const Stopwatch stopwatch;
    const auto stage = [&reportStage](const char* name) {
        if (reportStage)
            reportStage(name);
    };

    stage("Reading the mod manager");
    initializeModManager(uses);
    stage("Building the file map");
    initializeDirectory(uses);
    stage("Classifying textures");
    classifyTextures(cmCache);

    if (m_params.shaderPatcher.isTruePBREnabled) {
        stage("Loading the TruePBR configs");
        Logger::info("Loading {} TruePBR configs", m_directory->pbrJSONs().size());
        PatcherMeshShaderTruePBR::loadStatics(m_directory->pbrJSONs(), m_params.processing.multithread);
    }

    if (m_modManager) {
        // Shader attribution of every mod is complete now, so the mod list and the priorities are the offline ones.
        stage("Ordering the mods");
        m_modManager->updateStateFromModlist(m_params.modManager.shouldUseMO2LooseFileOrder);
    }

    stage("Registering the patchers");
    registerPatchers(m_params, m_ini);

    Logger::info("Runtime environment ready in {} ms", stopwatch.milliseconds());
}

void Environment::initializeModManager(const Uses& uses)
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

    PGGlobals::setPGMM(m_modManager.get());

    // A mod has meshes when it owns a mesh with uses: what the offline mesh reading pass sets.
    if (m_modManager->modFileMap().empty())
        return;

    std::size_t modsWithMeshes = 0;
    for (const auto& mod : m_modManager->mods()) {
        const std::shared_lock lock(mod->mutex);
        if (mod->hasMeshes)
            modsWithMeshes++;
    }

    for (const auto& [path, recordUses] : uses.byPath()) {
        // Only loose meshes resolve here; archived ones are attributed once the file map knows their archive.
        const auto mod = m_modManager->modByFile(path);
        if (!mod)
            continue;

        const std::unique_lock lock(mod->mutex);
        mod->hasMeshes = true;
    }

    Logger::debug("{} mods had meshes before the uses were applied", modsWithMeshes);
}

void Environment::initializeDirectory(const Uses& uses)
{
    const Stopwatch stopwatch;

    m_directory = std::make_unique<PGDirectory>(Settings::dataFolder(), std::filesystem::path());
    m_directory->setFoldersToMap(foldersToMap);
    m_directory->setShouldSkipHiddenFiles(false);
    m_directory->setArchiveLoadOrder(engineArchiveOrder());
    m_directory->setFileReader([](const std::filesystem::path& relPath) {
        std::vector<std::byte> bytes;
        if (!Resources::readFile(StringUtil::utf16toWindows1252(relPath.wstring()), bytes))
            throw std::runtime_error("The engine cannot read " + StringUtil::utf16toUTF8(relPath.wstring()));

        return bytes;
    });

    Logger::info("Building the file map");
    m_directory->populateFileMap(true, m_params.processing.multithread);

    // Meshes stay in the map only when they have uses, so a mod rule or a conflict can be attributed to the archive a
    // mesh comes from without keeping every mesh of the load order.
    const auto& used = uses.byPath();
    m_directory->retainFiles([&used](const std::filesystem::path& path) {
        const auto& folder = path.begin()->wstring();
        if (folder != L"meshes")
            return true;

        return used.contains(path);
    });

    // The reads go through the engine from here on; the archive tables are not needed anymore.
    m_directory->dropArchiveContents();

    PGGlobals::setPGD(m_directory.get());

    if (m_modManager && !m_modManager->modFileMap().empty()) {
        for (const auto& [path, recordUses] : used) {
            if (!m_directory->isBSAFile(path))
                continue;

            const auto mod = m_modManager->modByFileSmart(path);
            if (!mod)
                continue;

            const std::unique_lock lock(mod->mutex);
            mod->hasMeshes = true;
        }
    }

    Logger::info("File map of {} files built in {} ms", m_directory->fileMap().size(), stopwatch.milliseconds());
}

void Environment::classifyTextures(CMCache& cmCache)
{
    const Stopwatch stopwatch;
    const auto cachedBefore = cmCache.size();

    Logger::info("Classifying textures");
    m_directory->mapTexturesBySuffix(m_params.processing.textureMaps, m_params.processing.vanillaBSAList);
    cmCache.save();

    Logger::info("Classified {} textures in {} ms ({} complex material classifications added to the cache)",
                 m_directory->textures().size(),
                 stopwatch.milliseconds(),
                 cmCache.size() - cachedBefore);
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

    // The parallax to complex material transform and the SSS fix generate textures, which the runtime does not do.
    if (params.postPatcher.disablePrePatchedMaterials)
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::factory());
    if (params.postPatcher.isHairFlowMapEnabled)
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostHairFlowMap::factory());

    PGPatcher::loadPatchers(meshPatchers, { });
}

}
