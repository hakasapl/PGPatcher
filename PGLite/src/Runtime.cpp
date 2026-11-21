#include "PCH.h"

#include "Runtime.hpp"

#include "ClassificationCache.hpp"
#include "ConsoleCommands.hpp"
#include "Environment.hpp"
#include "Hooks.hpp"
#include "LightPlacerBridge.hpp"
#include "Logging.hpp"
#include "Menu.hpp"
#include "MeshReloadFramework.h"
#include "OfflineOutput.hpp"
#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "PGRunObserver.hpp"
#include "PGTranslation.hpp"
#include "PatchStream.hpp"
#include "Plan.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "ShaderFrameworks.hpp"
#include "TextureGenerator.hpp"
#include "Uses.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"
#include "util/TaskPoolRunner.hpp"

#include <boost/crc.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr const wchar_t* autoParallaxDLL = L"AutoParallax.dll";

/// @brief The longest the data-load handler waits for the initialization before the main menu opens without it.
constexpr auto maxInitializationWait = std::chrono::minutes(10);
constexpr auto initializationPollInterval = std::chrono::milliseconds(50);

std::string pathString(const std::filesystem::path& path) { return StringUtil::utf16toWindows1252(path.wstring()); }

/// @brief The path a record model loads now, spelled as streams are registered.
std::string modelPath(const RE::TESModel& model,
                      bool isWeightCounterpart)
{
    const auto path = Uses::normalizedModelPath(model.GetModel());
    return pathString(isWeightCounterpart ? Uses::weightCounterpart(path) : path);
}

/// @brief The longest Apply holds for MeshReloadFramework to report the reloads it asked for done.
constexpr auto maxReloadWait = std::chrono::seconds(30);

/// @brief The exports of MeshReloadFramework, whose presence turns the hot reload of Apply and the watches on.
struct MeshReloadFramework {
    MRF_ReloadMeshFn reloadMesh = nullptr;
    MRF_ReloadBaseFn reloadBase = nullptr;
    MRF_ForEachLoadedModelFn forEachLoadedModel = nullptr;
    MRF_RegisterReloadDoneCallbackFn registerReloadDoneCallback = nullptr;
    MRF_RegisterWatchProviderFn registerWatchProvider = nullptr;
    MRF_NotifyStreamChangedFn notifyStreamChanged = nullptr;
    MRF_WatchFileFn watchFile = nullptr;
    MRF_UnwatchFileFn unwatchFile = nullptr;
};

template<class Function>
Function frameworkExport(HMODULE module,
                         const char* name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

/// @brief The framework's exports, nullptr when it is not loaded. SKSE loads every plugin before the game reads data.
const MeshReloadFramework* meshReloadFramework()
{
    static const auto framework = [] -> std::optional<MeshReloadFramework> {
        auto* const module = GetModuleHandleW(L"MeshReloadFramework.dll");
        if (!module)
            return std::nullopt;

        const MeshReloadFramework exports {
            .reloadMesh = frameworkExport<MRF_ReloadMeshFn>(module, "MRF_ReloadMesh"),
            .reloadBase = frameworkExport<MRF_ReloadBaseFn>(module, "MRF_ReloadBase"),
            .forEachLoadedModel = frameworkExport<MRF_ForEachLoadedModelFn>(module, "MRF_ForEachLoadedModel"),
            .registerReloadDoneCallback
            = frameworkExport<MRF_RegisterReloadDoneCallbackFn>(module, "MRF_RegisterReloadDoneCallback"),
            .registerWatchProvider = frameworkExport<MRF_RegisterWatchProviderFn>(module, "MRF_RegisterWatchProvider"),
            .notifyStreamChanged = frameworkExport<MRF_NotifyStreamChangedFn>(module, "MRF_NotifyStreamChanged"),
            .watchFile = frameworkExport<MRF_WatchFileFn>(module, "MRF_WatchFile"),
            .unwatchFile = frameworkExport<MRF_UnwatchFileFn>(module, "MRF_UnwatchFile"),
        };
        if (!exports.reloadMesh || !exports.reloadBase || !exports.forEachLoadedModel
            || !exports.registerReloadDoneCallback || !exports.registerWatchProvider || !exports.notifyStreamChanged
            || !exports.watchFile || !exports.unwatchFile) {
            Logger::warn("MeshReloadFramework.dll lacks the exports PGLite uses, Apply does not reload loaded meshes");
            return std::nullopt;
        }

        Logger::info("MeshReloadFramework found, Apply reloads the loaded meshes it changes");
        return exports;
    }();
    return framework ? &*framework : nullptr;
}

/**
 * @brief A path as MeshReloadFramework spells it in its reports and callbacks: backslashes and ASCII lower case. The
 * paths PGLite registers have their folder prefix and no leading backslash already.
 */
std::string frameworkSpelling(std::string_view path)
{
    std::string spelled(path);
    for (auto& ch : spelled)
        if (ch == '/')
            ch = '\\';
        else if (ch >= 'A' && ch <= 'Z')
            ch = static_cast<char>(ch - 'A' + 'a');

    return spelled;
}

/**
 * @brief Ends the framework's watches of the files that no watched path is patched from any longer. Under the lock of
 * the watched paths.
 */
void releaseFiles(const std::unordered_map<std::string,
                                           std::vector<std::string>>& watchedPaths,
                  const std::vector<std::string>& files,
                  MRF_FileChangedFn callback,
                  void* context)
{
    const auto* const framework = meshReloadFramework();
    for (const auto& file : files) {
        const bool isStillWatched = std::ranges::any_of(watchedPaths, [&file](const auto& watched) {
            return std::ranges::find(watched.second, file) != watched.second.end();
        });
        if (!isStillWatched)
            std::ignore = framework->unwatchFile(file.c_str(), callback, context);
    }
}

/// @brief Shows the in-game notice on the main thread, once the game runs its tasks.
void showNotice(const std::string& text)
{
    SKSE::GetTaskInterface()->AddTask([text] { RE::DebugMessageBox(text.c_str()); });
}

/**
 * @brief The task guard of the shared code at runtime: a failed task is logged and the others carry on. The offline
 * guard latches the exception and stops every queue, which would stop PGLite for the session.
 */
bool runtimeTaskGuard(const std::function<void()>& task)
{
    try {
        task();
    } catch (const std::exception& e) {
        Logger::error("A background task failed: {}", e.what());
        return false;
    }

    return true;
}

}

Runtime::Runtime()
    : m_textures(Settings::textureCacheFolder())
{
}

Runtime& Runtime::instance()
{
    // Never destroyed: the registry holds the streams for the session, and the engine's heap is gone when static
    // destructors run.
    static auto* const runtime = new Runtime; // NOLINT(cppcoreguidelines-owning-memory)
    return *runtime;
}

bool Runtime::load()
{
    m_ini = Settings::readIni();
    Logging::initialize(m_ini.logLevel);
    ExceptionHandler::setTaskGuard(&runtimeTaskGuard);
    // The menu's strings; the stage names the runtime reports are keys of the same table.
    PGTranslation::init(Settings::translationsFolder(), Settings::readLanguage());
    if (PGTranslation::availableLanguages().empty())
        Logger::warn(L"No translation files in {}, the menu shows its keys instead of its texts",
                     Settings::translationsFolder().wstring());

    const auto version = REL::Module::get().version();
    Logger::info("PGLite {} loading (runtime {})", PG_FULL_VERSION, version.string("."));

    const auto addressLibrary = Resources::addressLibraryFile();
    std::error_code ec;
    if (!std::filesystem::exists(addressLibrary, ec)) {
        // Nothing of the engine can be used without it, not even the in-game notice.
        disable(PGTranslation::trFormat("pglite.disabled.addressLibraryMissing",
                                        StringUtil::utf16toUTF8(addressLibrary.filename().wstring())),
                false);
        return true;
    }

    if (!SKSE::GetMessagingInterface()->RegisterListener(&Runtime::onMessage)) {
        disable(PGTranslation::tr("pglite.disabled.noMessaging"), false);
        return true;
    }

    // The menu's redirects write their calls into the SKSE trampoline (Hooks). The interface draws are redirected now,
    // before anything renders, through the Address Library checked above, and draw nothing until the menu exists.
    SKSE::AllocTrampoline(Hooks::trampolineSize);
    Hooks::installEarly();

    return true;
}

PGParams Runtime::params() const
{
    const std::scoped_lock lock(m_stateMutex);
    return m_params;
}

std::optional<std::string> Runtime::disableReason() const
{
    const std::scoped_lock lock(m_stateMutex);
    return m_disableReason;
}

void Runtime::setMenuKeyEnabled(bool isEnabled)
{
    // Runtime only, as PGLite.ini stays the player's: the plugin that turned the key off says so again every session.
    if (m_isMenuKeyEnabled.exchange(isEnabled) != isEnabled)
        Logger::info("Another plugin {} the menu key", isEnabled ? "enabled" : "disabled");
}

Runtime::Status Runtime::status() const
{
    Status status;
    status.isActive = m_isActive;
    status.isReady = m_isReady;
    status.isEnabled = m_isEnabled;
    status.disableReason = disableReason();
    status.usedMeshes = m_uses.byPath().size();
    for (const auto& [path, uses] : m_uses.byPath())
        status.uses += uses.size();
    if (m_plan) {
        status.families = m_plan->families().size();
        for (const auto& family : m_plan->families())
            if (family->isFacegen)
                status.facegenFamilies++;
        status.patchedFamilies = m_plan->patchedFamilyCount();
        status.variants = m_plan->variantCount();
    }
    {
        const std::scoped_lock lock(m_streamsMutex);
        status.streams = m_streams.size();
    }
    status.textureSets = m_textureSets.size();
    const auto textures = m_textures.stats();
    status.texturesFromCache = textures.fromCache;
    status.texturesGenerated = textures.generated;
    status.texturesFailed = textures.failed;
    if (m_isReady && m_environment)
        status.mo2Instance = m_environment->mo2InstanceDir();
    status.offlineOutput = m_offlineOutput;
    status.stage = m_stage;

    return status;
}

void Runtime::onMessage(SKSE::MessagingInterface::Message* message)
{
    if (!message)
        return;

    auto& runtime = instance();
    if (message->type == SKSE::MessagingInterface::kInputLoaded) {
        runtime.onInputLoaded();
        return;
    }
    if (message->type != SKSE::MessagingInterface::kDataLoaded)
        return;

    // A disabled PGLite installs nothing, no hooks, no menu, no streams: the notice is all the game sees of it.
    if (const auto reason = runtime.disableReason()) {
        showNotice(PGTranslation::trFormat("pglite.disabled.notice", *reason));
        return;
    }

    runtime.detectShaderFrameworks();
    if (const auto reason = runtime.detectDisableCondition()) {
        runtime.disable(*reason, true);
        return;
    }

    // The menu's hooks draw the loading line into the frames presented during the wait below.
    runtime.m_menu = std::make_unique<Menu>(runtime);
    if (Hooks::install(*runtime.m_menu))
        runtime.m_installedMenu = runtime.m_menu.get();
    else
        runtime.m_menu.reset();
    ConsoleCommands::install();

    // This message comes on the thread that loaded the data, while the main thread waits for the load and opens the
    // main menu the moment it completes, and a loading-screen thread presents frames. The work is done here, on the
    // thread that owns the records at this point as every plugin's data-load handler does, and the handler then waits
    // until the initialization is done, so the main menu opens with PGLite ready (DESIGN.md section 4.1).
    runtime.onDataLoaded();
    runtime.waitForInitialization();
}

void Runtime::onInputLoaded()
{
    // The conditions that disable PGLite are checked again at data load, which shows the notice; here they only say
    // whether the environment is worth building. Nothing here may take the game down: a failure leaves the build
    // to the data load, which reports it.
    try {
        if (disableReason())
            return;

        detectShaderFrameworks();
        if (detectDisableCondition())
            return;

        readSettings();
        if (!m_isEnabled)
            return;

        if (!Environment::canOrderArchivesEarly()) {
            Logger::warn("Plugins.txt cannot be read, the environment is built after the data load, which orders the "
                         "archives by the loaded plugins");
            return;
        }

        Logger::info("Building the environment while the engine loads the plugins");
        m_earlyInitialization = std::async(std::launch::async, [this] { prepareEnvironment(); });
    } catch (const std::exception& e) {
        Logger::error("The environment is built after the data load instead of during the plugin load: {}", e.what());
    }
}

void Runtime::waitForEarlyInitialization()
{
    if (!m_earlyInitialization.valid())
        return;

    try {
        m_earlyInitialization.get();
    } catch (const std::exception& e) {
        Logger::error("The environment build started at the input load failed: {}", e.what());
    }
}

void Runtime::detectShaderFrameworks()
{
    if (m_didDetectShaderFrameworks)
        return;

    m_didDetectShaderFrameworks = true;
    m_shaderFrameworks = ShaderFrameworks::detect();
    Logger::info("Shader frameworks: Community Shaders {}, ENB {}",
                 m_shaderFrameworks.hasCommunityShaders ? "yes" : "no",
                 m_shaderFrameworks.hasENB ? "yes" : "no");
}

void Runtime::readSettings()
{
    if (m_areSettingsRead)
        return;

    {
        // A first start, without a settings.json, enables the patchers the loaded framework renders; a file keeps
        // what it has, less what the framework cannot render.
        PGParams params;
        if (Settings::hasSettingsFile()) {
            params = Settings::readParams();
        } else {
            Logger::info("No settings.json yet: the patchers of the loaded shader framework are enabled");
            params = ShaderFrameworks::firstStartParams(m_shaderFrameworks);

            // The mod manager and the MO2 instance default to the detected ones (PGModManager::detect).
            const auto detection = PGModManager::detect(Settings::dataFolder());
            params.modManager.type = detection.type;
            params.modManager.mo2InstanceDir = detection.mo2InstanceDir;
        }
        ShaderFrameworks::limitPatchers(params, m_shaderFrameworks);

        // A configured instance whose profile is not the game's (the game runs under another instance or profile)
        // would attribute the mods wrongly. The game's Plugins.txt is the profile's only through the virtual file
        // system.
        const auto instanceDir = Environment::resolvedMO2InstanceDir(params.modManager.mo2InstanceDir);
        if (params.modManager.type == PGModManager::ModManagerType::ModOrganizer2 && !instanceDir.empty()
            && !PGModManager::mo2DirFromUSVFS().empty()
            && !PGModManager::matchesActivePlugins(instanceDir, Environment::activePluginsFile())) {
            Logger::warn(L"The active plugins of the Mod Organizer 2 instance {} are not the game's: the game runs "
                         L"under another instance or profile; set the instance the game runs under in the PGLite menu",
                         instanceDir.wstring());
        }

        const std::scoped_lock lock(m_stateMutex);
        m_params = std::move(params);
    }
    m_areSettingsRead = true;

    m_isEnabled = Settings::readIsEnabled();
    for (const auto& problem : params().validate()) {
        // The menu refuses an Apply with these; an edited settings.json runs as it is, and the patchers it names
        // guard themselves (Environment::registerPatchers).
        Logger::error("settings.json: {}", PGTranslation::trFormat(problem.key, StringUtil::utf16toUTF8(problem.item)));
    }
}

void Runtime::waitForInitialization() const
{
    if (!m_isActive)
        return;

    const auto started = std::chrono::steady_clock::now();
    while (!m_isReady && !disableReason()) {
        if (std::chrono::steady_clock::now() - started > maxInitializationWait) {
            Logger::warn("The initialization did not finish in {} minutes, the main menu opens without it",
                         maxInitializationWait.count());
            return;
        }

        std::this_thread::sleep_for(initializationPollInterval);
    }

    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Logger::info("The main menu was held for {} ms", elapsed.count());
}

void Runtime::onDataLoaded()
{
    try {
        const auto started = std::chrono::steady_clock::now();
        readSettings();
        // The time since the previous lap, for the breakdown of the summary line.
        auto lastLap = std::chrono::steady_clock::now();
        const auto lap = [&lastLap] {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLap).count();
            lastLap = now;
            return elapsed;
        };

        long long offlineOutputMs = 0;
        if (OfflineOutput::hasStockRecords()) {
            // The output's records go back to stock before the uses are read, so the plan is the plan of the stock
            // records; its meshes are reverted by their PG_STOCK blocks when they are patched (DESIGN.md 4.14).
            Logger::info("An offline output generated in PGLite mode is enabled, putting its records back to stock");
            const auto restored = OfflineOutput::restore();
            Logger::info("Offline output: {} record models put back to stock, {} keep another plugin's value, {} "
                         "copies of its records mapped to stock, {} not in the loaded game, {} generated textures "
                         "left out of the file map",
                         restored.restored,
                         restored.kept,
                         restored.mapped,
                         restored.unresolved,
                         restored.generatedTextures.size());
            m_offlineOutput = restored;
            offlineOutputMs = lap();
        }

        Logger::info("Reading the record model uses");
        m_uses.build();
        m_snapshot.capture(m_uses);
        Logger::debug("Captured {} record models", m_snapshot.size());
        const auto usesMs = lap();

        auto plan = std::make_unique<Plan>(++m_planGeneration);
        if (m_isEnabled) {
            plan->build(m_uses, params(), m_variantCounter);
        } else {
            // The plan stays empty and nothing is registered: the game loads its meshes as if PGLite were absent.
            Logger::info("PGLite is disabled in settings.json; meshes stay unpatched until it is enabled in the menu");
        }
        m_plan = std::move(plan);
        const auto planMs = lap();
        long long streamsMs = 0;
        long long recordsMs = 0;
        if (m_isEnabled) {
            // The streams first, so a record is only pointed at a variant path that is served.
            registerPlanStreams();
            streamsMs = lap();
            m_plan->retargetRecords(m_snapshot);
            recordsMs = lap();
        }
        LightPlacerBridge::update(*m_plan);
        const auto lightPlacerMs = lap();

        m_facegenPaths = facegenPaths();
        const auto facegenMs = lap();
        m_isActive = true;
        connectMeshReloadFramework();

        const auto elapsed
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        Logger::info("Data-load work done in {} ms: offline output {} ms, uses {} ms, plan {} ms, streams {} ms, "
                     "records {} ms, light placer {} ms, facegen paths {} ms",
                     elapsed.count(),
                     offlineOutputMs,
                     usesMs,
                     planMs,
                     streamsMs,
                     recordsMs,
                     lightPlacerMs,
                     facegenMs);

        std::thread([this] { initializeInBackground(); }).detach();
    } catch (const std::exception& e) {
        // The records go back to what they were, whatever step failed.
        m_snapshot.restore();
        disable(PGTranslation::trFormat("pglite.disabled.initializationFailed", e.what()), true);
    }
}

std::optional<std::string> Runtime::detectDisableCondition() const
{
    // An offline output is accepted when it was generated in PGLite mode: its stock records let PGLite put the
    // records back (OfflineOutput). Any other is for playing without PGLite.
    if (!OfflineOutput::hasStockRecords() && OfflineOutput::isPresent())
        return PGTranslation::tr("pglite.disabled.offlineOutput");

    std::error_code ec;
    if (GetModuleHandleW(autoParallaxDLL) || std::filesystem::exists(Settings::pluginsFolder() / autoParallaxDLL, ec))
        return PGTranslation::tr("pglite.disabled.autoParallax");

    if (!m_shaderFrameworks.hasAny())
        return PGTranslation::tr("pglite.disabled.noShaderFramework");

    return std::nullopt;
}

void Runtime::disable(const std::string& reason,
                      bool shouldShowInGame)
{
    {
        const std::scoped_lock lock(m_stateMutex);
        m_disableReason = reason;
    }

    {
        const std::scoped_lock lock(m_contentMutex);
        m_reloadBytes.clear();
    }

    Logger::critical("PGLite is disabled because {}", reason);

    // A build started at the input load is joined before the runtime counts as ready, so nothing reads the
    // environment it would still be assigning.
    waitForEarlyInitialization();

    // Whoever waits for the initialization is released: every stream serves the original mesh from now on.
    {
        const std::scoped_lock lock(m_readyMutex);
        m_isReady = true;
    }
    m_readyCondition.notify_all();

    const auto text = PGTranslation::trFormat("pglite.disabled.notice", reason);
    if (shouldShowInGame)
        showNotice(text);
    else
        MessageBoxW(nullptr, StringUtil::utf8toUTF16(text).c_str(), L"PGLite", MB_OK | MB_ICONWARNING);
}

std::shared_ptr<SourceStream> Runtime::resolveSource(const std::filesystem::path& path)
{
    // A registered path already has its source.
    const auto pathStr = pathString(path);
    if (auto source = registeredSource(pathStr))
        return source;

    RE::BSTSmartPointer<RE::BSResource::Stream> engineStream;
    if (!Resources::resolveStream(pathStr, engineStream))
        return nullptr;

    auto source = std::make_shared<SourceStream>();
    source->stream = std::move(engineStream);
    source->path = pathStr;
    return source;
}

std::shared_ptr<SourceStream> Runtime::registeredSource(const std::string& path) const
{
    const std::scoped_lock lock(m_streamsMutex);
    const auto it = m_streams.find(path);
    return it == m_streams.end() ? nullptr : it->second.source;
}

void Runtime::registerStream(const std::filesystem::path& path,
                             std::shared_ptr<SourceStream> source)
{
    const auto pathStr = pathString(path);
    {
        const std::scoped_lock lock(m_streamsMutex);
        if (m_streams.contains(pathStr))
            return;
    }

    RE::BSTSmartPointer<RE::BSResource::Stream> stream(new PatchStream(pathStr, source, *this));
    Resources::registerStream(pathStr, stream);

    const std::scoped_lock lock(m_streamsMutex);
    m_streams[pathStr] = { .stream = std::move(stream), .source = std::move(source) };
}

void Runtime::registerPlanStreams()
{
    // The folder of the grass meshes the grass loader copies the streams of.
    constexpr std::string_view grassMeshFolder = R"(meshes\landscape\grass\)";

    bool hasNewGrassStream = false;
    std::size_t registered = 0;
    std::size_t unresolved = 0;
    std::size_t partial = 0;
    for (const auto& family : m_plan->families()) {
        if (family->isFacegen || !family->sources.empty())
            continue;

        std::vector<std::shared_ptr<SourceStream>> sources(family->basePaths.size());
        std::size_t resolved = 0;
        for (std::size_t pathIndex = 0; pathIndex < family->basePaths.size(); pathIndex++) {
            sources.at(pathIndex) = resolveSource(family->basePaths.at(pathIndex));
            if (sources.at(pathIndex)) {
                resolved++;
            } else {
                // Like a missing mesh offline: nothing to patch, the engine keeps looking for it itself.
                Logger::debug(L"Not registered, the engine cannot resolve {}",
                              family->basePaths.at(pathIndex).wstring());
            }
        }

        if (!resolved) {
            unresolved++;
            continue;
        }

        // A weighted pair with one file the engine cannot resolve is patched on the other file alone.
        if (resolved < family->basePaths.size())
            partial++;

        family->sources = std::move(sources);
        for (const auto& member : family->members) {
            for (std::size_t pathIndex = 0; pathIndex < member.paths.size(); pathIndex++) {
                if (!family->sources.at(pathIndex))
                    continue;

                const auto& path = member.paths.at(pathIndex);
                if (!hasNewGrassStream) {
                    // The loader copies the folder's own files, not those of its subfolders.
                    const auto pathStr = pathString(path);
                    const bool isGrassMesh = pathStr.starts_with(grassMeshFolder)
                        && pathStr.find('\\', grassMeshFolder.size()) == std::string::npos;
                    hasNewGrassStream = isGrassMesh && !registeredSource(pathStr);
                }

                registerStream(path, family->sources.at(pathIndex));
                registered++;
            }
        }
    }

    Logger::info("Registered {} mesh streams ({} families have no mesh the engine can resolve, {} weighted pairs "
                 "have one)",
                 registered,
                 unresolved,
                 partial);

    // The grass loader copies the grass streams registered when it runs (Resources), and a copy is a clone of the
    // stream registered here, which reads the plan of the moment at every open. It runs again whenever a grass stream
    // it has no copy of was registered: at the first registration, since its own runs came before it, and at an Apply
    // that registers a grass path for the first time, whose grass the hot reload then builds from the new stream.
    if (hasNewGrassStream) {
        Resources::refreshGrassStreams();
        Logger::debug("The grass loader took the registered grass streams");
    }
}

std::vector<std::filesystem::path> Runtime::facegenPaths() const
{
    std::vector<std::filesystem::path> paths;
    if (m_ini.shouldExcludeFacegens)
        return paths;

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        return paths;

    // Read at data load, before the game runs: the form arrays grow when the game creates forms.
    const auto folder = StringUtil::utf16toUTF8(PGNIFUtil::facegenFolder);
    for (const auto* const form : dataHandler->GetFormArray(RE::FormType::NPC)) {
        if (!form)
            continue;

        const auto* const file = form->GetFile(0);
        if (!file)
            continue;

        auto path = std::format("{}\\{}\\{:08X}.nif", folder, file->GetFilename(), form->GetLocalFormID());
        StringUtil::toLowerASCIIFastInPlace(path);
        const std::filesystem::path widePath = StringUtil::windows1252toUTF16(path);
        if (m_uses.byPath().contains(widePath)) {
            // A facegen path with record uses is left unpatched, with the plan's warning (Plan::build).
            continue;
        }

        paths.push_back(widePath);
    }

    return paths;
}

void Runtime::registerFacegenStreams()
{
    std::size_t registered = 0;
    for (const auto& path : m_facegenPaths) {
        auto source = resolveSource(path);
        if (!source)
            continue;

        auto& family = m_plan->addFacegenFamily(path);
        family.sources.push_back(source);
        registerStream(path, std::move(source));
        registered++;
    }

    Logger::info("Registered {} facegen streams of {} NPC forms", registered, m_facegenPaths.size());
}

void Runtime::reportStage(const char* stage)
{
    m_progressTotal = 0;
    m_progressDone = 0;
    m_stage = stage;
}

void Runtime::reportProgress(std::size_t done,
                             std::size_t total)
{
    m_progressTotal = total;
    m_progressDone = done;
}

void Runtime::prepareEnvironment()
{
    if (!PGGlobals::isPGD3DSet()) {
        reportStage("progress.steps.initGpu");
        Logger::info("Initializing the GPU");
        if (!m_d3d.initGPU() || !m_d3d.initShaders())
            throw std::runtime_error("the GPU could not be initialized");

        PGGlobals::setPGD3D(&m_d3d);
    }

    // The cache is the process's observer: the classifications of the mesh reading pass, the registrations the
    // texture hooks make, which reach the texture generator through it, and the texture files a patch could not read,
    // which keep that patch from being kept (Family::bytes).
    m_cache.setHookRegistrationListener([](PGRunObserver::HookKind kind, const std::filesystem::path& texPath) {
        TextureGenerator::noteRegistration(kind, texPath);
    });
    m_cache.setTextureReadFailureListener(
        [](const std::filesystem::path& texPath) { Family::noteTextureReadFailure(texPath); });
    PGRunObserver::install(&m_cache);

    buildEnvironment([this](const char* stage) { reportStage(stage); },
                     [this](std::size_t done, std::size_t total) { reportProgress(done, total); });
}

void Runtime::initializeInBackground()
{
    const auto started = std::chrono::steady_clock::now();
    // The time since the previous lap, for the breakdown of the summary line.
    auto lastLap = started;
    const auto lap = [&lastLap] {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastLap).count();
        lastLap = now;
        return elapsed;
    };
    long long facegenMs = 0;
    long long environmentMs = 0;
    try {
        if (m_isEnabled) {
            // The stage of a build started at the input load stays on the loading line, with its progress.
            if (!m_earlyInitialization.valid())
                reportStage("pglite.stages.registeringFacegenStreams");
            registerFacegenStreams();
            facegenMs = lap();
            if (m_earlyInitialization.valid()) {
                // Built since the input load; its own lines say what it took. A failure of that thread is thrown here.
                m_earlyInitialization.get();
                environmentMs = lap();
                Logger::info("The environment was built while the engine loaded the plugins, waited {} ms for it",
                             environmentMs);
            } else {
                prepareEnvironment();
                environmentMs = lap();
            }
            Environment::reportArchivesOfUnloadedPlugins();
        } else {
            // The Apply that enables PGLite builds the environment; until then nothing is read.
            Logger::info("PGLite is disabled in settings.json, the environment is built when it is enabled");
        }
    } catch (const std::exception& e) {
        // Every stream serves the original mesh then.
        disable(PGTranslation::trFormat("pglite.disabled.backgroundInitializationFailed", e.what()), true);
    }

    m_stage = nullptr;
    m_progressTotal = 0;
    {
        const std::scoped_lock lock(m_readyMutex);
        m_isReady = true;
    }
    m_readyCondition.notify_all();

    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Logger::info("PGLite is ready ({} ms after the data load: facegen streams {} ms, environment {} ms)",
                 elapsed.count(),
                 facegenMs,
                 environmentMs);
}

void Runtime::buildEnvironment(const std::function<void(const char*)>& reportStage,
                               const Environment::ProgressCallback& reportProgress)
{
    reportStage("pglite.stages.loadingClassificationCache");
    const auto cacheStarted = std::chrono::steady_clock::now();
    m_cache.load(Settings::classificationCacheFile());
    Logger::info(
        "Classification cache loaded in {} ms ({} meshes, {} complex material classifications)",
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - cacheStarted).count(),
        m_cache.meshCount(),
        m_cache.cmCount());

    // The textures an enabled offline output generated are inputs of no patch (OfflineOutput), whether its records
    // were put back to stock already or not.
    auto environment = std::make_unique<Environment>(params(), m_ini, OfflineOutput::generatedTextures());
    environment->initialize(m_cache, reportStage, reportProgress);
    // The pass saved what it added; the entries are read again by the next Apply, not kept for the session.
    m_cache.unload();

    m_environment = std::move(environment);
    // The textures generated so far stay known to the new directory, as the hooks made them known to the old one.
    m_textures.onEnvironmentReplaced(*m_environment->directory());
}

bool Runtime::waitUntilReady()
{
    if (m_isReady)
        return true;
    if (m_didInitializationExpire)
        return false;

    std::unique_lock lock(m_readyMutex);
    if (m_readyCondition.wait_for(lock, maxInitializationWait, [this] { return m_isReady.load(); }))
        return true;

    // The environment is still being built: nothing patches against it, and no later open waits for it again.
    if (!m_didInitializationExpire.exchange(true)) {
        Logger::error("The initialization did not finish in {} minutes, the original meshes are served",
                      maxInitializationWait.count());
    }

    return false;
}

void Runtime::prepareApply()
{
    m_loadedModels.reset();
    const auto* const framework = meshReloadFramework();
    if (!framework || !m_isActive || !m_isReady || !m_plan || disableReason())
        return;

    // A loading screen's frame is drawn by another thread while the main thread loads the cells.
    if (auto* const ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME))
        return;

    try {
        std::vector<LoadedModel> models;
        framework->forEachLoadedModel(
            [](const MRF_LoadedModel& loaded, void* context) {
                static_cast<std::vector<LoadedModel>*>(context)->push_back({
                    .path = loaded.path,
                    .model = static_cast<const RE::TESModel*>(loaded.model),
                    .formID = loaded.formID,
                    .isWeightCounterpart = loaded.isWeightCounterpart,
                });
            },
            &models);

        // A model shows what PGLite last served its path, or the engine's original for a path PGLite did not register.
        // A registered path PGLite never served was never loaded.
        std::vector<std::uint8_t> isServed(models.size(), 0);
        {
            const std::scoped_lock lock(m_contentMutex);
            for (std::size_t index = 0; index < models.size(); index++) {
                if (const auto it = m_servedContent.find(models.at(index).path); it != m_servedContent.end()) {
                    models.at(index).shown = it->second;
                    isServed.at(index) = 1;
                }
            }
        }
        for (std::size_t index = models.size(); index-- > 0;)
            if (!isServed.at(index) && registeredSource(models.at(index).path))
                models.erase(models.begin() + static_cast<std::ptrdiff_t>(index));
        for (auto& model : models)
            if (model.model && !model.isWeightCounterpart)
                model.alternateTextures = alternateTextures(*model.model);

        Logger::debug("The loaded game shows {} models", models.size());
        m_loadedModels = std::move(models);
    } catch (const std::exception& e) {
        Logger::error("The loaded meshes are not reloaded by this Apply: {}", e.what());
    }
}

bool Runtime::apply(const PGParams& requestedParams,
                    bool isEnabled)
{
    // What the loaded game showed before this Apply.
    const auto loadedModels = std::exchange(m_loadedModels, std::nullopt);
    m_didCheckLoadedMeshes = false;
    {
        const std::scoped_lock reloadsLock(m_pendingReloadsMutex);
        m_pendingReloads = { };
    }
    if (!m_isActive || !m_isReady || !m_plan || disableReason()) {
        Logger::warn("Apply refused, PGLite is not ready");
        return false;
    }

    // The menu keeps the patchers the framework cannot render off already; an edited settings.json does not.
    auto params = requestedParams;
    ShaderFrameworks::limitPatchers(params, m_shaderFrameworks);

    const auto started = std::chrono::steady_clock::now();
    const std::unique_lock lock(m_pipelineLock);
    {
        // Bytes the last Apply kept for its reload come from the plan this one replaces.
        const std::scoped_lock contentLock(m_contentMutex);
        m_reloadBytes.clear();
    }

    try {
        m_stage = "pglite.stages.restoringRecords";
        m_snapshot.restore();
        {
            const std::scoped_lock stateLock(m_stateMutex);
            m_params = params;
        }
        m_isEnabled = isEnabled;

        if (!isEnabled) {
            // Every stream serves the original mesh from now on; the environment is rebuilt by the apply that enables.
            m_plan = std::make_unique<Plan>(++m_planGeneration);
            LightPlacerBridge::update(*m_plan);
            reloadChangedMeshes(loadedModels);
            m_stage = nullptr;
            Logger::info("PGLite is disabled; every mesh is served as the original from now on");
            return true;
        }

        // A session that started disabled has neither the GPU nor the observer: the Apply that enables it prepares them
        // before its build, as the start does.
        if (PGGlobals::isPGD3DSet())
            buildEnvironment([this](const char* stage) { m_stage = stage; }, { });
        else
            prepareEnvironment();

        m_stage = "pglite.stages.planningMeshes";
        auto plan = std::make_unique<Plan>(++m_planGeneration);
        plan->build(m_uses, params, m_variantCounter);
        m_plan = std::move(plan);
        m_stage = "pglite.stages.registeringStreams";
        registerPlanStreams();
        registerFacegenStreams();
        // The streams first, so a record is only pointed at a variant path that is served.
        m_plan->retargetRecords(m_snapshot);
        LightPlacerBridge::update(*m_plan);
        reloadChangedMeshes(loadedModels);
    } catch (const std::exception& e) {
        // The records go back to their originals and every stream serves the original mesh; the status page shows
        // PGLite disabled, and its Enable button is the next attempt.
        Logger::critical("Apply failed, PGLite serves original meshes until the next apply: {}", e.what());
        m_snapshot.restore();
        m_isEnabled = false;
        m_plan = std::make_unique<Plan>(++m_planGeneration);
        LightPlacerBridge::update(*m_plan);
        // The loaded models show the originals too, as they would after a cell reload.
        reloadChangedMeshes(loadedModels);
        m_stage = nullptr;
        return false;
    }

    m_stage = nullptr;
    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Logger::info("Applied the settings in {} ms", elapsed.count());
    return true;
}

std::filesystem::path Runtime::originalMeshPath(const RE::TESModel& model)
{
    auto path = Uses::normalizedModelPath(model.GetModel());
    if (path.empty())
        return path;

    // A record PGLite pointed at a variant path names a member of the family of its original file.
    const std::shared_lock lock(m_pipelineLock);
    const auto* const target = m_plan ? m_plan->target(pathString(path)) : nullptr;
    if (target && target->family)
        return target->family->basePaths.at(target->pathIndex);

    return path;
}

Runtime::BlockListChange Runtime::setMeshBlocked(const std::filesystem::path& meshPath,
                                                 bool isBlocked)
{
    const auto mesh = meshPath.wstring();
    // The rules match without case (PGDirectory::checkGlobMatchInVector), so a rule of the mesh can have any.
    const auto isRuleOfMesh = [&mesh](const std::wstring& rule) { return StringUtil::asciiFastIEquals(rule, mesh); };

    const std::scoped_lock lock(m_stateMutex);
    auto blockList = m_params.processing.blockList;
    if (isBlocked) {
        if (std::ranges::any_of(blockList, isRuleOfMesh))
            return BlockListChange::Unchanged;

        blockList.push_back(mesh);
    } else if (!std::erase_if(blockList, isRuleOfMesh)) {
        return BlockListChange::Unchanged;
    }

    if (!Settings::writeBlockList(blockList, m_params))
        return BlockListChange::NotWritten;

    m_params.processing.blockList = std::move(blockList);
    return isBlocked ? BlockListChange::Added : BlockListChange::Removed;
}

std::shared_ptr<const std::vector<std::byte>> Runtime::bytesFor(const std::string& path,
                                                                SourceStream& source)
{
    {
        const std::scoped_lock lock(m_contentMutex);
        if (const auto node = m_reloadBytes.extract(path)) {
            m_servedContent[path] = contentKey(*node.mapped());
            return node.mapped();
        }
    }

    ProducedMesh produced;
    bool didFail = false;
    // Held until what the path serves is booked below, so that an Apply cannot book its own in between.
    std::shared_lock pipelineLock(m_pipelineLock, std::defer_lock);
    try {
        if (!waitUntilReady()) {
            produced = { .bytes = readOriginal(path, source), .isOriginal = true };
        } else {
            pipelineLock.lock();
            produced = produce(path, source);
        }
    } catch (const std::exception& e) {
        Logger::error("Unable to produce {}, the original mesh is served: {}", path, e.what());
        didFail = true;
    } catch (...) {
        Logger::error("Unable to produce {}, the original mesh is served", path);
        didFail = true;
    }

    if (didFail) {
        try {
            produced = { .bytes = readOriginal(path, source), .isOriginal = true };
        } catch (...) {
            return nullptr;
        }
    }

    if (produced.bytes && meshReloadFramework()) {
        // What a model loaded from the path shows from now on, which the next Apply compares with. The group's path
        // serves the same bytes once the records load it; it is booked only when it holds nothing yet, as a model may
        // still show an older one from it.
        const auto content = produced.isOriginal ? originalServed : contentKey(*produced.bytes);
        const std::scoped_lock lock(m_contentMutex);
        m_servedContent[path] = content;
        if (!produced.groupPath.empty())
            m_servedContent.try_emplace(produced.groupPath, content);
    }

    return std::move(produced.bytes);
}

Runtime::ProducedMesh Runtime::produce(const std::string& path,
                                       SourceStream& source)
{
    const auto* const target = m_plan ? m_plan->target(path) : nullptr;
    if (target && target->family && !target->family->sources.empty() && !disableReason()) {
        auto bytes = target->family->bytes(target->member, target->pathIndex, m_textureSets, m_plan->generation());
        // The textures the patch registered with the texture hooks are served before the engine parses the mesh that
        // names them (DESIGN.md section 4.12).
        m_textures.generatePending();
        ProducedMesh produced { .bytes = std::move(bytes) };
        auto groupPath = pathString(target->family->groupPath(target->member, target->pathIndex));
        if (groupPath != path)
            produced.groupPath = std::move(groupPath);
        return produced;
    }

    // A path of no current family (a variant of an earlier plan, or PGLite is disabled): the original mesh.
    return { .bytes = readOriginal(path, source), .isOriginal = true };
}

std::shared_ptr<const std::vector<std::byte>> Runtime::readOriginal(const std::string& path,
                                                                    SourceStream& source)
{
    auto bytes = std::make_shared<std::vector<std::byte>>();
    const std::scoped_lock sourceLock(source.mutex);
    if (!source.stream || !Resources::readStream(*source.stream, *bytes)) {
        Logger::error("Unable to read the original mesh of {}", path);
        return nullptr;
    }

    return bytes;
}

std::uint64_t Runtime::contentKey(const std::vector<std::byte>& bytes)
{
    boost::crc_32_type crc;
    crc.process_bytes(bytes.data(), bytes.size());
    return (static_cast<std::uint64_t>(bytes.size()) << 32U) ^ crc.checksum();
}

std::optional<std::uint64_t> Runtime::originalContent(const std::string& path)
{
    // The registered stream would serve the plan's bytes: the engine's own stream has the original.
    if (const auto source = registeredSource(path)) {
        const auto bytes = readOriginal(path, *source);
        if (!bytes)
            return std::nullopt;

        return contentKey(*bytes);
    }

    std::vector<std::byte> bytes;
    if (!Resources::readFile(path, bytes))
        return std::nullopt;

    return contentKey(bytes);
}

std::vector<std::pair<std::int32_t,
                      const RE::BGSTextureSet*>>
Runtime::alternateTextures(const RE::TESModel& model) const
{
    std::vector<std::pair<std::int32_t, const RE::BGSTextureSet*>> entries;
    if (const auto* const swap = m_snapshot.swap(model))
        for (const auto& entry : Uses::alternateTextures(*swap))
            entries.emplace_back(entry.index3D, entry.textureSet);

    return entries;
}

void Runtime::reloadChangedMeshes(const std::optional<std::vector<LoadedModel>>& models)
{
    const auto* const framework = meshReloadFramework();
    if (!framework || !models || models->empty())
        return;

    reportStage("pglite.stages.patchingLoadedMeshes");
    const auto started = std::chrono::steady_clock::now();
    try {
        {
            // A path keeps what its models show until they load it again, so that a path this Apply registers or leaves
            // unsettled does not read as never served at the next one.
            const std::scoped_lock lock(m_contentMutex);
            for (const auto& model : *models)
                m_servedContent.try_emplace(model.path, model.shown);
        }

        // The paths the models load now, each patched and compared by one task.
        struct PathCheck {
            std::string path;
            /// @brief The path the records load once merged members point at their group (Family::groupPath).
            std::string loadPath;
            std::vector<std::size_t> models;
            /// @brief The key of the patch result served now, nothing for the original.
            std::optional<std::uint64_t> content;
            /// @brief False for a first patch that could not read an input, which serves the original until retried.
            bool isSettled = false;
            bool hasStream = false;
        };
        std::vector<PathCheck> checks;
        {
            std::unordered_map<std::string, std::size_t> checkOfPath;
            for (std::size_t index = 0; index < models->size(); index++) {
                const auto& model = models->at(index);
                auto path = model.model ? modelPath(*model.model, model.isWeightCounterpart) : model.path;
                const auto [it, isNew] = checkOfPath.try_emplace(path, checks.size());
                if (isNew)
                    checks.push_back({ .path = path, .loadPath = std::move(path) });
                checks.at(it->second).models.push_back(index);
            }
        }

        // The original of a path, read once and only when one side of a comparison is the original.
        std::unordered_map<std::string, std::optional<std::uint64_t>> originals;
        std::mutex originalsMutex;
        const auto originalOf = [this, &originals, &originalsMutex](const std::string& path) {
            {
                const std::scoped_lock lock(originalsMutex);
                if (const auto it = originals.find(path); it != originals.end())
                    return it->second;
            }
            const auto content = originalContent(path);
            const std::scoped_lock lock(originalsMutex);
            return originals.emplace(path, content).first->second;
        };

        // Per model: bit 0 its bytes changed, bit 1 its record's alternate textures did. Each model has one check.
        std::vector<std::uint8_t> changes(models->size(), 0);
        const auto compare = [this, &models, &changes, &originalOf](PathCheck& check) {
            const auto* const target = m_plan ? m_plan->target(check.path) : nullptr;
            check.isSettled = !target || !target->family || target->family->state() != Family::State::Unpatched;
            if (!check.isSettled)
                return;

            for (const auto index : check.models) {
                const auto& model = models->at(index);
                auto before = model.shown == originalServed ? std::nullopt : std::optional(model.shown);
                auto after = check.content;
                if (before != after && (!before || !after)) {
                    before = before ? before : originalOf(model.path);
                    after = after ? after : originalOf(check.path);
                }
                // The other file of a weighted pair shares the record, whose entries the named file compares.
                const bool isRetextured = model.model && !model.isWeightCounterpart
                    && alternateTextures(*model.model) != model.alternateTextures;
                changes.at(index) = static_cast<std::uint8_t>((before != after ? 1U : 0U) | (isRetextured ? 2U : 0U));
            }
        };

        TaskPoolRunner runner;
        for (auto& check : checks) {
            runner.addTask([this, &check, &compare, &changes] {
                // A path without a stream is the engine's own file, compared once its family's other file was patched.
                const auto source = registeredSource(check.path);
                if (!source)
                    return;

                check.hasStream = true;
                const auto produced = produce(check.path, *source);
                if (!produced.bytes)
                    return;

                if (!produced.isOriginal) {
                    check.content = contentKey(*produced.bytes);
                    if (!produced.groupPath.empty())
                        check.loadPath = produced.groupPath;
                }
                // The produce ran the first patch of the family, which set the alternate textures of its records.
                compare(check);
                const bool isChanged = std::ranges::any_of(
                    check.models, [&changes](std::size_t index) { return (changes.at(index) & 1U) != 0; });
                if (check.isSettled && isChanged && !produced.isOriginal) {
                    const std::scoped_lock lock(m_contentMutex);
                    m_reloadBytes.insert_or_assign(check.path, produced.bytes);
                    m_reloadBytes.insert_or_assign(check.loadPath, produced.bytes);
                }
            });
        }
        runner.runTasks();
        for (auto& check : checks)
            if (!check.hasStream)
                compare(check);

        // A changed path is booked once a load reads it (bytesFor); until then it keeps what its models showed, so the
        // next Apply finds a model the reload did not reach changed again. A member's load path can be a changed
        // group's, so those are known first.
        const auto isContentChanged = [&changes](const PathCheck& check) {
            return std::ranges::any_of(check.models,
                                       [&changes](std::size_t index) { return (changes.at(index) & 1U) != 0; });
        };
        std::unordered_set<std::string> changedPaths;
        for (const auto& check : checks) {
            if (check.isSettled && isContentChanged(check)) {
                changedPaths.insert(check.path);
                changedPaths.insert(check.loadPath);
            }
        }

        std::unordered_set<RE::FormID> retexturedForms;
        std::size_t unsettled = 0;
        {
            const std::scoped_lock lock(m_contentMutex);
            for (const auto& check : checks) {
                if (!check.isSettled) {
                    unsettled++;
                    m_servedContent.try_emplace(check.path, unknownContent);
                    m_servedContent.try_emplace(check.loadPath, unknownContent);
                    continue;
                }
                if (isContentChanged(check)) {
                    m_servedContent.try_emplace(check.path, unknownContent);
                    m_servedContent.try_emplace(check.loadPath, unknownContent);
                    continue;
                }

                // Same bytes under changed alternate textures: the records that changed them reload their references.
                for (const auto index : check.models)
                    if ((changes.at(index) & 2U) != 0 && models->at(index).formID)
                        retexturedForms.insert(models->at(index).formID);

                const auto content = check.content.value_or(originalServed);
                for (const auto* const path : { &check.path, &check.loadPath })
                    if (check.hasStream && !changedPaths.contains(*path))
                        m_servedContent[*path] = content;
            }
        }

        // Apply holds until the framework reported these reloads done (isReloadPending). Each is booked before it is
        // asked for, so that no report can come first, and unbooked when the framework refuses it.
        {
            const std::scoped_lock lock(m_pendingReloadsMutex);
            m_pendingReloads.askedTime = std::chrono::steady_clock::now();
            for (const auto& path : changedPaths)
                m_pendingReloads.paths.insert(frameworkSpelling(path));
            m_pendingReloads.bases.insert(retexturedForms.begin(), retexturedForms.end());
            m_pendingReloads.askedCount = m_pendingReloads.paths.size() + m_pendingReloads.bases.size();
        }
        for (const auto& path : changedPaths) {
            if (!framework->reloadMesh(path.c_str())) {
                const std::scoped_lock lock(m_pendingReloadsMutex);
                m_pendingReloads.askedCount -= m_pendingReloads.paths.erase(frameworkSpelling(path));
            }
        }
        for (const auto formID : retexturedForms) {
            if (!framework->reloadBase(formID)) {
                const std::scoped_lock lock(m_pendingReloadsMutex);
                m_pendingReloads.askedCount -= m_pendingReloads.bases.erase(formID);
            }
        }
        m_didCheckLoadedMeshes = true;

        Logger::info(
            "{} of {} loaded models changed: reloading {} meshes and the references of {} retextured records ({} "
            "meshes "
            "not settled are left as they are), checked in {} ms",
            std::ranges::count_if(changes, [](std::uint8_t change) { return change != 0; }),
            models->size(),
            changedPaths.size(),
            retexturedForms.size(),
            unsettled,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
    } catch (const std::exception& e) {
        Logger::error("The loaded meshes are not reloaded, their changes show at their next load: {}", e.what());
    }
}

bool Runtime::isReloadPending()
{
    const std::scoped_lock lock(m_pendingReloadsMutex);
    auto& pending = m_pendingReloads;
    if (pending.paths.empty() && pending.bases.empty())
        return false;

    const auto waited = std::chrono::steady_clock::now() - pending.askedTime;
    if (waited < maxReloadWait)
        return true;

    // The framework reports what it gave up on as well, so a report this late is not coming.
    const auto example = pending.paths.empty() ? std::format("{:08X}", *pending.bases.begin()) : *pending.paths.begin();
    Logger::warn("MeshReloadFramework did not report {} of the {} reloads Apply asked for done in {} s, Apply ends "
                 "without them (one of them: {})",
                 pending.paths.size() + pending.bases.size(),
                 pending.askedCount,
                 std::chrono::duration_cast<std::chrono::seconds>(waited).count(),
                 example);
    pending.paths.clear();
    pending.bases.clear();
    return false;
}

std::pair<std::size_t,
          std::size_t>
Runtime::reloadProgress() const
{
    const std::scoped_lock lock(m_pendingReloadsMutex);
    const auto remaining
        = std::min(m_pendingReloads.paths.size() + m_pendingReloads.bases.size(), m_pendingReloads.askedCount);
    return { m_pendingReloads.askedCount - remaining, m_pendingReloads.askedCount };
}

void Runtime::connectMeshReloadFramework()
{
    const auto* const framework = meshReloadFramework();
    if (!framework)
        return;

    if (!framework->registerReloadDoneCallback(onReloadDone, this))
        Logger::error("MeshReloadFramework refused PGLite's callback for its done reports");

    if (framework->registerWatchProvider(onWatch, onUnwatch, this))
        Logger::info("PGLite takes MeshReloadFramework's watches of the meshes it serves");
    else
        Logger::error("MeshReloadFramework refused PGLite as the watch provider of the meshes it serves");
}

void Runtime::onReloadDone(const MRF_ReloadDone& done,
                           void* context)
{
    auto& runtime = *static_cast<Runtime*>(context);
    const std::scoped_lock lock(runtime.m_pendingReloadsMutex);
    auto& pending = runtime.m_pendingReloads;
    if (pending.paths.empty() && pending.bases.empty())
        return;

    // Every caller's reloads are reported, a watch's and the console's too: Apply's are the ones it booked.
    for (const auto* const path : std::span(done.paths, done.pathCount))
        if (path)
            pending.paths.erase(path);
    for (const auto formID : std::span(done.bases, done.baseCount))
        pending.bases.erase(formID);

    if (pending.paths.empty() && pending.bases.empty()) {
        Logger::info(
            "MeshReloadFramework reloaded the {} meshes and records Apply asked for, {} ms after the request",
            pending.askedCount,
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - pending.askedTime)
                .count());
    }
}

bool Runtime::onWatch(const char* path,
                      void* context)
{
    // Nothing may leave a callback into the framework's code.
    try {
        return path != nullptr && static_cast<Runtime*>(context)->watchPath(frameworkSpelling(path));
    } catch (const std::exception& e) {
        Logger::error("Unable to watch {} for MeshReloadFramework: {}", path, e.what());
    } catch (...) {
        Logger::error("Unable to watch {} for MeshReloadFramework", path);
    }

    return false;
}

void Runtime::onUnwatch(const char* path,
                        void* context)
{
    try {
        if (path)
            static_cast<Runtime*>(context)->unwatchPath(frameworkSpelling(path));
    } catch (const std::exception& e) {
        Logger::error("Unable to end the watch of {} for MeshReloadFramework: {}", path, e.what());
    } catch (...) {
        Logger::error("Unable to end the watch of {} for MeshReloadFramework", path);
    }
}

void Runtime::onFileChanged(const char* file,
                            void* context)
{
    try {
        if (file)
            static_cast<Runtime*>(context)->reloadChangedFile(frameworkSpelling(file));
    } catch (const std::exception& e) {
        Logger::error("Unable to patch the meshes made of {} again: {}", file, e.what());
    } catch (...) {
        Logger::error("Unable to patch the meshes made of {} again", file);
    }
}

bool Runtime::watchPath(const std::string& path)
{
    const auto* const framework = meshReloadFramework();
    auto files = looseInputs(path);
    if (files.empty()) {
        Logger::info("MeshReloadFramework asked PGLite to watch {}, which PGLite patches from no loose mesh file: the "
                     "watch is declined",
                     path);
        return false;
    }

    std::string fileList;
    for (const auto& file : files)
        fileList += (fileList.empty() ? "" : ", ") + file;

    const std::scoped_lock lock(m_watchedPathsMutex);
    for (const auto& file : files)
        if (!framework->watchFile(file.c_str(), onFileChanged, this))
            Logger::warn("MeshReloadFramework refused to watch {} for PGLite", file);

    // A path watched again can be patched from other files than at its first watch, after an Apply.
    auto& watched = m_watchedPaths[path];
    const auto previous = std::exchange(watched, std::move(files));
    releaseFiles(m_watchedPaths, previous, onFileChanged, this);
    Logger::info("MeshReloadFramework watches {}, which PGLite patches again when {} changes", path, fileList);
    return true;
}

void Runtime::unwatchPath(const std::string& path)
{
    const std::scoped_lock lock(m_watchedPathsMutex);
    const auto node = m_watchedPaths.extract(path);
    if (node.empty())
        return;

    releaseFiles(m_watchedPaths, node.mapped(), onFileChanged, this);
    Logger::info("MeshReloadFramework no longer watches {}", path);
}

std::vector<std::string> Runtime::looseInputs(const std::string& path)
{
    // A path of no stream of PGLite's, or a generated texture's, which reads no mesh.
    const auto source = registeredSource(path);
    if (!source || source->path.empty())
        return { };

    std::vector<std::string> meshes { source->path };
    if (m_isReady) {
        // The background initialization adds the facegen families to the plan before PGLite is ready.
        const std::shared_lock lock(m_pipelineLock);
        if (const auto* const target = m_plan ? m_plan->target(path) : nullptr; target && target->family)
            for (const auto& basePath : target->family->basePaths)
                meshes.push_back(pathString(basePath));
    }

    std::vector<std::string> files;
    for (const auto& mesh : meshes) {
        auto file = frameworkSpelling(mesh);
        if (std::ranges::find(files, file) != files.end())
            continue;

        // The game reads the loose files it had at its start, which the mod manager's virtual file system shows here.
        std::error_code ec;
        if (std::filesystem::is_regular_file(Settings::dataFolder() / StringUtil::windows1252toUTF16(mesh), ec))
            files.push_back(std::move(file));
    }

    return files;
}

void Runtime::reloadChangedFile(const std::string& file)
{
    const auto* const framework = meshReloadFramework();
    std::unordered_set<std::string> watched;
    {
        const std::scoped_lock lock(m_watchedPathsMutex);
        for (const auto& [path, files] : m_watchedPaths)
            if (std::ranges::find(files, file) != files.end())
                watched.insert(path);
    }
    if (watched.empty())
        return;

    // Every registered path made of the file: the paths of the families patched from it, which forget their patch so
    // that their next open patches the file as it is now, and the streams that read it, which serve its original
    // otherwise. Nothing is patched before PGLite is ready.
    std::unordered_set<std::string> changed;
    std::size_t families = 0;
    if (m_isReady) {
        const std::shared_lock lock(m_pipelineLock);
        if (m_plan) {
            for (const auto& family : m_plan->families()) {
                const bool isPatchedFromFile
                    = std::ranges::any_of(family->basePaths, [&file](const std::filesystem::path& basePath) {
                          return frameworkSpelling(pathString(basePath)) == file;
                      });
                if (!isPatchedFromFile)
                    continue;

                family->forgetPatch();
                families++;
                for (const auto& member : family->members)
                    for (std::size_t pathIndex = 0; pathIndex < member.paths.size(); pathIndex++)
                        if (pathIndex < family->sources.size() && family->sources.at(pathIndex))
                            changed.insert(pathString(member.paths.at(pathIndex)));
            }
        }
    }
    {
        const std::scoped_lock lock(m_streamsMutex);
        for (const auto& [path, entry] : m_streams)
            if (entry.source && frameworkSpelling(entry.source->path) == file)
                changed.insert(path);
    }

    std::vector<std::string> others;
    {
        const std::scoped_lock lock(m_contentMutex);
        for (const auto& path : changed) {
            // Bytes the last Apply kept for a reload are made of the file as it was.
            m_reloadBytes.erase(path);
            if (!watched.contains(frameworkSpelling(path)) && m_servedContent.contains(path))
                others.push_back(path);
        }
    }

    // The watched paths reload what watches them. A path made of the file that the game loaded and no watch names
    // reloads too, as everything that shows a changed loose file does without PGLite: the other contexts of the mesh.
    for (const auto& path : watched)
        std::ignore = framework->notifyStreamChanged(path.c_str());
    for (const auto& path : others)
        std::ignore = framework->reloadMesh(path.c_str());

    Logger::info("{} changed: its {} families are patched again at their next load, MeshReloadFramework reloads the {} "
                 "watched paths made of it and {} other paths loaded from it",
                 file,
                 families,
                 watched.size(),
                 others.size());
}

}
