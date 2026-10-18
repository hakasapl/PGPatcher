#include "PCH.h"

#include "Runtime.hpp"

#include "ClassificationCache.hpp"
#include "Environment.hpp"
#include "Hooks.hpp"
#include "LightPlacerBridge.hpp"
#include "Logging.hpp"
#include "Menu.hpp"
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

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <system_error>
#include <thread>
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
    if (!Hooks::install(*runtime.m_menu))
        runtime.m_menu.reset();

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
    const auto pathStr = pathString(path);
    {
        // A registered path already has its source.
        const std::scoped_lock lock(m_streamsMutex);
        const auto it = m_streams.find(pathStr);
        if (it != m_streams.end())
            return it->second.source;
    }

    RE::BSTSmartPointer<RE::BSResource::Stream> engineStream;
    if (!Resources::resolveStream(pathStr, engineStream))
        return nullptr;

    auto source = std::make_shared<SourceStream>();
    source->stream = std::move(engineStream);
    return source;
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

                registerStream(member.paths.at(pathIndex), family->sources.at(pathIndex));
                registered++;
            }
        }
    }

    Logger::info("Registered {} mesh streams ({} families have no mesh the engine can resolve, {} weighted pairs "
                 "have one)",
                 registered,
                 unresolved,
                 partial);

    // The grass loader took its copies of the grass streams before this registration (Resources), so it takes them
    // again, once: the copies it makes now are clones of the streams registered here, and a clone reads the plan of
    // the moment at every open, so a later registration changes nothing for it.
    if (registered && !m_didRefreshGrassStreams) {
        Resources::refreshGrassStreams();
        m_didRefreshGrassStreams = true;
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
    m_cache.setHookRegistrationListener([this](PGRunObserver::HookKind kind, const std::filesystem::path& texPath) {
        m_textures.noteRegistration(kind, texPath);
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

bool Runtime::apply(const PGParams& requestedParams,
                    bool isEnabled)
{
    if (!m_isActive || !m_isReady || !m_plan || disableReason()) {
        Logger::warn("Apply refused, PGLite is not ready");
        return false;
    }

    // The menu keeps the patchers the framework cannot render off already; an edited settings.json does not.
    auto params = requestedParams;
    ShaderFrameworks::limitPatchers(params, m_shaderFrameworks);

    const auto started = std::chrono::steady_clock::now();
    const std::unique_lock lock(m_pipelineLock);
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
            m_stage = nullptr;
            Logger::info("PGLite is disabled; meshes loaded from now on are the originals");
            return true;
        }

        buildEnvironment([this](const char* stage) { m_stage = stage; }, { });

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
    } catch (const std::exception& e) {
        // The records go back to their originals and every stream serves the original mesh; the status page shows
        // PGLite disabled, and its Enable button is the next attempt.
        Logger::critical("Apply failed, PGLite serves original meshes until the next apply: {}", e.what());
        m_snapshot.restore();
        m_isEnabled = false;
        m_plan = std::make_unique<Plan>(++m_planGeneration);
        LightPlacerBridge::update(*m_plan);
        m_stage = nullptr;
        return false;
    }

    m_stage = nullptr;
    const auto elapsed
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    Logger::info("Applied the settings in {} ms", elapsed.count());
    return true;
}

std::shared_ptr<const std::vector<std::byte>> Runtime::bytesFor(const std::string& path,
                                                                SourceStream& source)
{
    const auto readOriginal = [&path, &source]() -> std::shared_ptr<const std::vector<std::byte>> {
        auto bytes = std::make_shared<std::vector<std::byte>>();
        const std::scoped_lock sourceLock(source.mutex);
        if (!source.stream || !Resources::readStream(*source.stream, *bytes)) {
            Logger::error("Unable to read the original mesh of {}", path);
            return nullptr;
        }

        return bytes;
    };

    try {
        if (!waitUntilReady())
            return readOriginal();

        const std::shared_lock lock(m_pipelineLock);
        auto* const target = m_plan ? m_plan->target(path) : nullptr;
        if (target && target->family && !target->family->sources.empty() && !disableReason()) {
            auto bytes = target->family->bytes(target->member, target->pathIndex, m_textureSets, m_plan->generation());
            // The textures the patch registered with the texture hooks are served before the engine parses the mesh
            // that names them (DESIGN.md section 4.12).
            m_textures.generatePending();
            return bytes;
        }

        // A path of no current family (a variant of an earlier plan, or PGLite is disabled): the original mesh.
        return readOriginal();
    } catch (const std::exception& e) {
        Logger::error("Unable to produce {}, the original mesh is served: {}", path, e.what());
    } catch (...) {
        Logger::error("Unable to produce {}, the original mesh is served", path);
    }

    try {
        return readOriginal();
    } catch (...) {
        return nullptr;
    }
}

}
