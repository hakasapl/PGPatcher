#include "PCH.h"

#include "Runtime.hpp"

#include "Logging.hpp"
#include "PatchStream.hpp"
#include "Resources.hpp"
#include "Settings.hpp"
#include "Uses.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
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

std::optional<std::string> Runtime::disableReason() const
{
    const std::scoped_lock lock(m_stateMutex);
    return m_disableReason;
}

std::size_t Runtime::streamCount() const
{
    const std::scoped_lock lock(m_streamsMutex);
    return m_streams.size();
}

void Runtime::onMessage(SKSE::MessagingInterface::Message* message)
{
    if (!message || message->type != SKSE::MessagingInterface::kDataLoaded)
        return;

    // One frame later, after every plugin's data-load handler ran.
    SKSE::GetTaskInterface()->AddTask([] { instance().onDataLoaded(); });
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

        Logger::info("Reading the record model uses");
        m_uses.build();

        registerStreams();
        m_isActive = true;

        auto facegen = facegenPaths();
        std::thread([this, paths = std::move(facegen)]() mutable { initializeInBackground(std::move(paths)); })
            .detach();
    } catch (const std::exception& e) {
        disable(std::format("its initialization failed: {}", e.what()), true);
    }
}

std::optional<std::string> Runtime::detectDisableCondition() const
{
    if (Resources::exists(offlineDiffFile))
        return "an offline PGPatcher output is in the load order (ParallaxGen_Diff.json)";

    auto* const dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler
        && (dataHandler->LookupLoadedModByName(offlinePlugin) || dataHandler->LookupLoadedLightModByName(offlinePlugin)))
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

bool Runtime::registerStream(const std::string& path)
{
    RE::BSTSmartPointer<RE::BSResource::Stream> engineStream;
    if (!Resources::resolveStream(path, engineStream)) {
        // Like a missing mesh offline: nothing to patch, the engine keeps looking for it itself.
        Logger::debug("Not registered, the engine cannot resolve {}", path);
        return false;
    }

    auto source = std::make_shared<SourceStream>();
    source->stream = std::move(engineStream);

    RE::BSTSmartPointer<RE::BSResource::Stream> stream(new PatchStream(path, std::move(source), *this));
    Resources::registerStream(path, stream);

    const std::scoped_lock lock(m_streamsMutex);
    m_streams[path] = std::move(stream);
    return true;
}

void Runtime::registerStreams()
{
    std::size_t registered = 0;
    std::size_t unresolved = 0;
    for (const auto& [path, uses] : m_uses.byPath()) {
        if (registerStream(StringUtil::utf16toWindows1252(path.wstring())))
            registered++;
        else
            unresolved++;
    }

    Logger::info("Registered {} mesh streams, {} used meshes are not in the load order", registered, unresolved);
}

std::vector<std::string> Runtime::facegenPaths() const
{
    std::vector<std::string> paths;
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
        if (m_uses.byPath().contains(StringUtil::windows1252toUTF16(path))) {
            // Patched like any used mesh, which the offline program warns about and leaves unpatched.
            Logger::warn("Facegen mesh has record uses and is left unpatched: {}", path);
            continue;
        }

        paths.push_back(std::move(path));
    }

    return paths;
}

void Runtime::initializeInBackground(std::vector<std::string> facegenPaths)
{
    try {
        std::size_t registered = 0;
        for (const auto& path : facegenPaths)
            if (registerStream(path))
                registered++;

        Logger::info("Registered {} facegen streams of {} NPC forms", registered, facegenPaths.size());
    } catch (const std::exception& e) {
        Logger::critical("PGLite background initialization failed: {}", e.what());
    }

    {
        const std::scoped_lock lock(m_readyMutex);
        m_isReady = true;
    }
    m_readyCondition.notify_all();
    Logger::info("PGLite is ready");
}

void Runtime::waitUntilReady()
{
    if (m_isReady)
        return;

    std::unique_lock lock(m_readyMutex);
    m_readyCondition.wait(lock, [this] { return m_isReady.load(); });
}

std::shared_ptr<const std::vector<std::byte>> Runtime::bytesFor(const std::string& path,
                                                                SourceStream& source)
{
    try {
        auto bytes = std::make_shared<std::vector<std::byte>>();
        {
            const std::scoped_lock lock(source.mutex);
            if (!Resources::readStream(*source.stream, *bytes)) {
                Logger::error("Unable to read the original mesh of {}", path);
                return nullptr;
            }
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
