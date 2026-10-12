#include "PCH.h"

#include "Runtime.hpp"

#include "CMCache.hpp"
#include "Environment.hpp"
#include "Hooks.hpp"
#include "Logging.hpp"
#include "Menu.hpp"
#include "PGGlobals.hpp"
#include "PGParams.hpp"
#include "PGRunObserver.hpp"
#include "PatchStream.hpp"
#include "Plan.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
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
    if (m_isReady && m_environment)
        status.mo2Instance = m_environment->mo2InstanceDir();
    status.stage = m_stage;

    return status;
}

void Runtime::onMessage(SKSE::MessagingInterface::Message* message)
{
    if (!message || message->type != SKSE::MessagingInterface::kDataLoaded)
        return;

    // The development aid listens for the main menu from here on, which can open before the deferred task runs.
    if (!instance().m_ini.loadSaveAtStart.empty()) {
        if (auto* const ui = RE::UI::GetSingleton())
            ui->AddEventSink<RE::MenuOpenCloseEvent>(&instance());
    }

    // One frame later, after every plugin's data-load handler ran.
    SKSE::GetTaskInterface()->AddTask([] { instance().onDataLoaded(); });
}

void Runtime::onDataLoaded()
{
    try {
        // The menu exists whether or not PGLite is active, so it can state the reason when it is not.
        m_menu = std::make_unique<Menu>(*this);
        if (!Hooks::install(*m_menu))
            m_menu.reset();

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

        Logger::info("Reading the record model uses");
        m_uses.build();
        m_snapshot.capture(m_uses);
        Logger::debug("Captured {} record models", m_snapshot.size());

        auto plan = std::make_unique<Plan>(++m_planGeneration);
        plan->build(m_uses, params(), m_variantCounter);
        plan->retargetRecords();
        m_plan = std::move(plan);
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
    const auto reportStage = [this](const char* stage) { m_stage = stage; };
    try {
        if (!PGGlobals::isPGD3DSet()) {
            reportStage("Initializing the GPU");
            Logger::info("Initializing the GPU");
            if (!m_d3d.initGPU() || !m_d3d.initShaders())
                throw std::runtime_error("the GPU could not be initialized");

            PGGlobals::setPGD3D(&m_d3d);
        }

        reportStage("Loading the classification cache");
        m_cmCache.load(Settings::cmCacheFile());
        PGRunObserver::install(&m_cmCache);

        reportStage("Registering the facegen streams");
        registerFacegenStreams();

        auto environment = std::make_unique<Environment>(params(), m_ini);
        environment->initialize(m_uses, m_cmCache, reportStage);
        m_environment = std::move(environment);
    } catch (const std::exception& e) {
        // Every stream serves the original mesh then.
        disable(std::format("its background initialization failed: {}", e.what()), true);
    }

    m_stage = nullptr;
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

bool Runtime::apply(const PGParams& params)
{
    if (!m_isActive || !m_isReady || !m_plan || disableReason())
        return false;

    const auto started = std::chrono::steady_clock::now();
    const std::unique_lock lock(m_pipelineLock);
    try {
        m_stage = "Restoring the records";
        m_snapshot.restore();
        {
            const std::scoped_lock stateLock(m_stateMutex);
            m_params = params;
        }

        auto environment = std::make_unique<Environment>(params, m_ini);
        environment->initialize(m_uses, m_cmCache, [this](const char* stage) { m_stage = stage; });
        m_environment = std::move(environment);

        m_stage = "Planning the meshes";
        auto plan = std::make_unique<Plan>(++m_planGeneration);
        plan->build(m_uses, params, m_variantCounter);
        plan->retargetRecords();
        m_plan = std::move(plan);
        m_stage = "Registering the streams";
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
        if (target && target->family && !target->family->sources.empty() && !disableReason())
            return target->family->bytes(target->member, target->pathIndex, m_textureSets, m_plan->generation());

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
