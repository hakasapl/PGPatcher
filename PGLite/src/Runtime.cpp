#include "PCH.h"

#include "Runtime.hpp"

#include "ClassificationCache.hpp"
#include "Environment.hpp"
#include "Hooks.hpp"
#include "Logging.hpp"
#include "Menu.hpp"
#include "PGGlobals.hpp"
#include "PGParams.hpp"
#include "PGRunObserver.hpp"
#include "PGTranslation.hpp"
#include "PatchStream.hpp"
#include "Plan.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "TextureGenerator.hpp"
#include "Uses.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr const char* offlineDiffFile = "parallaxgen_diff.json";
constexpr const char* offlinePlugin = "PGPatcher.esp";
constexpr const wchar_t* autoParallaxDLL = L"AutoParallax.dll";

/// @brief The meshes folder of the facegen heads, see TESNPC and the offline PGNIFUtil::isFacegenMesh().
constexpr const char* facegenFolder = "meshes\\actors\\character\\facegendata\\facegeom";

/// @brief The longest the data-load handler waits for the initialization before the main menu opens without it.
constexpr auto maxInitializationWait = std::chrono::minutes(10);
constexpr auto initializationPollInterval = std::chrono::milliseconds(50);

std::string pathString(const std::filesystem::path& path) { return StringUtil::utf16toWindows1252(path.wstring()); }

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
    // The input redirect of the menu writes one call into the SKSE trampoline (Hooks).
    SKSE::AllocTrampoline(Hooks::trampolineSize);

    const auto version = REL::Module::get().version();
    Logger::info("PGLite {} loading (runtime {})", PG_FULL_VERSION, version.string("."));

    const auto addressLibrary = Resources::addressLibraryFile();
    if (!std::filesystem::exists(addressLibrary)) {
        // Nothing of the engine can be used without it, not even the in-game notice.
        const auto reason = std::format("the Address Library for this game version is missing ({})",
                                        StringUtil::utf16toUTF8(addressLibrary.filename().wstring()));
        disable(reason, false);
        return true;
    }

    if (!Resources::isRuntimeSupported()) {
        // The notice waits for the game UI.
        const std::scoped_lock lock(m_stateMutex);
        m_disableReason = std::format("game version {} is not supported", version.string("."));
        Logger::critical("PGLite is disabled because {}", *m_disableReason);
    }

    if (!SKSE::GetMessagingInterface()->RegisterListener(&Runtime::onMessage)) {
        disable("the SKSE messaging interface is unavailable", false);
        return true;
    }

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
    status.stage = m_stage;

    return status;
}

void Runtime::onMessage(SKSE::MessagingInterface::Message* message)
{
    if (!message || message->type != SKSE::MessagingInterface::kDataLoaded)
        return;

    auto& runtime = instance();

    // The development aid listens for the main menu from here on.
    if (!runtime.m_ini.loadSaveAtStart.empty()) {
        if (auto* const ui = RE::UI::GetSingleton())
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&runtime);
    }

    // The menu exists whether or not PGLite is active, so it can state the reason when it is not; its hooks draw the
    // loading line into the frames presented during the wait below.
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
        if (const auto reason = disableReason()) {
            RE::DebugMessageBox(std::format("PGLite is disabled because {}.", *reason).c_str());
            return;
        }

        if (const auto reason = detectDisableCondition()) {
            disable(*reason, true);
            return;
        }

        const auto started = std::chrono::steady_clock::now();
        {
            const std::scoped_lock lock(m_stateMutex);
            m_params = Settings::readParams();
        }

        m_isEnabled = Settings::readIsEnabled();
        Logger::info("Reading the record model uses");
        m_uses.build();
        m_snapshot.capture(m_uses);
        Logger::debug("Captured {} record models", m_snapshot.size());

        auto plan = std::make_unique<Plan>(++m_planGeneration);
        if (m_isEnabled) {
            plan->build(m_uses, params(), m_variantCounter);
            plan->retargetRecords();
        } else {
            // The plan stays empty and nothing is registered: the game loads its meshes as if PGLite were absent.
            Logger::info("PGLite is disabled in settings.json; meshes stay unpatched until it is enabled in the menu");
        }
        m_plan = std::move(plan);
        if (m_isEnabled)
            registerPlanStreams();

        m_facegenPaths = facegenPaths();
        m_isActive = true;

        const auto elapsed
            = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
        Logger::info("Data-load task done in {} ms", elapsed.count());

        std::thread([this] { initializeInBackground(); }).detach();

        if (!m_ini.loadSaveAtStart.empty()) {
            // The main menu may be open already, in which case no event comes.
            auto* const ui = RE::UI::GetSingleton();
            if (ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
                const RE::MenuOpenCloseEvent event { .menuName = RE::MainMenu::MENU_NAME, .opening = true };
                ProcessEvent(&event, nullptr);
            }
        }
    } catch (const std::exception& e) {
        disable(std::format("its initialization failed: {}", e.what()), true);
    }
}

RE::BSEventNotifyControl Runtime::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                               [[maybe_unused]] RE::BSTEventSource<RE::MenuOpenCloseEvent>* source)
{
    if (!event || !event->opening || event->menuName != RE::MainMenu::MENU_NAME || m_didLoadSaveAtStart.exchange(true))
        return RE::BSEventNotifyControl::kContinue;

    // A development aid (PGLite.ini [Debug] sLoadSaveAtStart): straight into a save, so a session can be tested
    // without clicking through the menu.
    Logger::info("Loading the save {} (sLoadSaveAtStart)", m_ini.loadSaveAtStart);
    SKSE::GetTaskInterface()->AddTask([save = m_ini.loadSaveAtStart] {
        if (auto* const manager = RE::BGSSaveLoadManager::GetSingleton())
            manager->Load(save.c_str());
    });

    return RE::BSEventNotifyControl::kContinue;
}

std::optional<std::string> Runtime::detectDisableCondition() const
{
    if (Resources::exists(offlineDiffFile))
        return "an offline PGPatcher output is in the load order (ParallaxGen_Diff.json)";

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler
        && (dataHandler->LookupLoadedModByName(offlinePlugin)
            || dataHandler->LookupLoadedLightModByName(offlinePlugin)))
        return "the offline PGPatcher plugin is active (PGPatcher.esp)";

    if (GetModuleHandleW(autoParallaxDLL) || std::filesystem::exists(Settings::pluginsFolder() / autoParallaxDLL))
        return "Auto Parallax is installed, which edits the same materials";

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

    const auto text = std::format("PGLite is disabled because {}.", reason);
    if (shouldShowInGame)
        RE::DebugMessageBox(text.c_str());
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
    for (const auto& family : m_plan->families()) {
        if (family->isFacegen || !family->sources.empty())
            continue;

        std::vector<std::shared_ptr<SourceStream>> sources;
        for (const auto& basePath : family->basePaths) {
            auto source = resolveSource(basePath);
            if (!source) {
                // Like a missing mesh offline: nothing to patch, the engine keeps looking for it itself.
                Logger::debug(L"Not registered, the engine cannot resolve {}", basePath.wstring());
                break;
            }

            sources.push_back(std::move(source));
        }

        if (sources.size() != family->basePaths.size()) {
            unresolved++;
            continue;
        }

        family->sources = std::move(sources);
        for (const auto& member : family->members) {
            for (std::size_t pathIndex = 0; pathIndex < member.paths.size(); pathIndex++) {
                registerStream(member.paths.at(pathIndex), family->sources.at(pathIndex));
                registered++;
            }
        }
    }

    Logger::info(
        "Registered {} mesh streams ({} families have a mesh the engine cannot resolve)", registered, unresolved);
}

std::vector<std::filesystem::path> Runtime::facegenPaths() const
{
    std::vector<std::filesystem::path> paths;
    if (m_ini.shouldExcludeFacegens)
        return paths;

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler)
        return paths;

    // Read on the main thread: the form arrays grow when the game creates forms.
    for (const auto* const form : dataHandler->GetFormArray(RE::FormType::NPC)) {
        if (!form)
            continue;

        const auto* const file = form->GetFile(0);
        if (!file)
            continue;

        auto path = std::format("{}\\{}\\{:08X}.nif", facegenFolder, file->GetFilename(), form->GetLocalFormID());
        StringUtil::toLowerASCIIFastInPlace(path);
        const std::filesystem::path widePath = StringUtil::windows1252toUTF16(path);
        if (m_uses.byPath().contains(widePath)) {
            // The offline program warns about this and leaves the mesh unpatched.
            Logger::warn("NIF has mesh uses but is detected as a facegen mesh: {}", path);
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

void Runtime::initializeInBackground()
{
    const auto started = std::chrono::steady_clock::now();
    const auto reportStage = [this](const char* stage) {
        m_progressTotal = 0;
        m_progressDone = 0;
        m_stage = stage;
    };
    const auto reportProgress = [this](std::size_t done, std::size_t total) {
        m_progressTotal = total;
        m_progressDone = done;
    };
    try {
        if (!PGGlobals::isPGD3DSet()) {
            reportStage("progress.steps.initGpu");
            Logger::info("Initializing the GPU");
            if (!m_d3d.initGPU() || !m_d3d.initShaders())
                throw std::runtime_error("the GPU could not be initialized");

            PGGlobals::setPGD3D(&m_d3d);
        }

        reportStage("pglite.stages.loadingClassificationCache");
        m_cache.load(Settings::classificationCacheFile());
        PGRunObserver::install(&m_cache);

        if (m_isEnabled) {
            reportStage("pglite.stages.registeringFacegenStreams");
            registerFacegenStreams();
        }

        auto environment = std::make_unique<Environment>(params(), m_ini);
        environment->initialize(m_cache, reportStage, reportProgress);
        m_environment = std::move(environment);
        m_textures.onEnvironmentReplaced(*m_environment->directory());
    } catch (const std::exception& e) {
        // Every stream serves the original mesh then.
        disable(std::format("its background initialization failed: {}", e.what()), true);
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
    Logger::info("PGLite is ready ({} ms)", elapsed.count());
}

void Runtime::waitUntilReady()
{
    if (m_isReady)
        return;

    std::unique_lock lock(m_readyMutex);
    m_readyCondition.wait(lock, [this] { return m_isReady.load(); });
}

bool Runtime::apply(const PGParams& params,
                    bool isEnabled)
{
    if (!m_isActive || !m_isReady || !m_plan || disableReason())
        return false;

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
            m_stage = nullptr;
            Logger::info("PGLite is disabled; meshes loaded from now on are the originals");
            return true;
        }

        auto environment = std::make_unique<Environment>(params, m_ini);
        environment->initialize(m_cache, [this](const char* stage) { m_stage = stage; });
        m_environment = std::move(environment);
        // The textures generated so far stay known to the new directory, as the hooks made them known to the old one.
        m_textures.onEnvironmentReplaced(*m_environment->directory());

        m_stage = "pglite.stages.planningMeshes";
        auto plan = std::make_unique<Plan>(++m_planGeneration);
        plan->build(m_uses, params, m_variantCounter);
        plan->retargetRecords();
        m_plan = std::move(plan);
        m_stage = "pglite.stages.registeringStreams";
        registerPlanStreams();
        registerFacegenStreams();
    } catch (const std::exception& e) {
        Logger::critical("Apply failed, PGLite serves original meshes until the next apply: {}", e.what());
        m_plan = std::make_unique<Plan>(++m_planGeneration);
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
    try {
        waitUntilReady();

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
        auto bytes = std::make_shared<std::vector<std::byte>>();
        const std::scoped_lock sourceLock(source.mutex);
        if (!source.stream || !Resources::readStream(*source.stream, *bytes)) {
            Logger::error("Unable to read the original mesh of {}", path);
            return nullptr;
        }

        return bytes;
    } catch (const std::exception& e) {
        Logger::error("Unable to produce {}: {}", path, e.what());
        return nullptr;
    } catch (...) {
        Logger::error("Unable to produce {}", path);
        return nullptr;
    }
}

}
