#include "PCH.h"

#include "Hooks.hpp"

#include "Menu.hpp"
#include "util/Logger.hpp"

#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <windows.h>

namespace PGLite {

namespace {

/// @brief The pause between two progress frames of Apply, on top of the swap chain's own pacing.
constexpr auto applyFrameInterval = std::chrono::milliseconds(10);

}

bool Hooks::install(Menu& menu)
{
    auto* const renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) {
        Logger::error("The renderer is unavailable, the menu is disabled");
        return false;
    }

    auto* const swapChain = reinterpret_cast<IDXGISwapChain*>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        renderer->GetRuntimeData().renderWindows[0].swapChain);
    if (!swapChain) {
        Logger::error("The swap chain is unavailable, the menu is disabled");
        return false;
    }

    // Address Library IDs of BSInputDeviceManager::PollInputDevices. The dispatch call sits at the same offset in SE
    // and AE (checked in the 1.7.99 database and in the 1.7.104 executable: `mov rcx, rsi` and `lea rdx, [rsp+40h]`
    // load the source and the address of the event list head right before it). Only the opcode is checked, because
    // another plugin that redirected the call first has already replaced its target.
    const REL::Relocation<std::uintptr_t> pollSite { REL::RelocationID(67315, 68617), pollDispatchCallOffset };
    const auto site = pollSite.address();
    const auto opcode
        = *reinterpret_cast<const std::uint8_t*>(site); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (opcode != callOpcode) {
        Logger::error(
            "The input dispatch call site holds {:#x} instead of a call ({:#x}), the menu is disabled", opcode, site);
        return false;
    }

    s_menu = &menu;

    // The vtable of the swap chain class; the previous entries are called after the menu, so other overlays chain.
    REL::Relocation<std::uintptr_t> vtable {
        *reinterpret_cast<std::uintptr_t*>(swapChain) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    };
    s_originalPresent = reinterpret_cast<decltype(&present)>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        vtable.write_vfunc(presentIndex, &present));
    s_originalResizeBuffers
        = reinterpret_cast<decltype(&resizeBuffers)>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            vtable.write_vfunc(resizeBuffersIndex, &resizeBuffers));

    s_originalDispatch = SKSE::GetTrampoline().write_call<5>(site, &dispatchInput);

    Logger::debug("Menu hooks installed (input dispatch call at {:#x} went to {:#x}, swap chain at {:#x})",
                  site,
                  s_originalDispatch.address(),
                  reinterpret_cast<std::uintptr_t>(swapChain)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return true;
}

void Hooks::dispatchInput(RE::BSTEventSource<RE::InputEvent*>* source,
                          RE::InputEvent* const* events)
{
    if (events && s_menu) {
        for (const auto* event = *events; event; event = event->next) {
            if (s_tracedEvents < maxTracedEvents) {
                s_tracedEvents++;
                const auto* const button = event->AsButtonEvent();
                Logger::debug("Input event {}: type {} device {} code {}",
                              s_tracedEvents,
                              static_cast<int>(event->GetEventType()),
                              static_cast<int>(event->GetDevice()),
                              button ? button->GetIDCode() : 0);
            }

            s_menu->handleInput(*event);
        }
    }

    if (s_menu && s_menu->isOpen()) {
        // The game sees no input while the menu is open. Keys still held resume when the devices poll them after the
        // close; the source is still called, as the engine does every frame.
        static constexpr RE::InputEvent* const noEvents[] = { nullptr };
        s_originalDispatch(source, noEvents);
        return;
    }

    s_originalDispatch(source, events);
}

HRESULT WINAPI Hooks::present(IDXGISwapChain* swapChain,
                              UINT syncInterval,
                              UINT flags)
{
    if (s_menu) {
        s_menu->render(swapChain);

        // Apply runs on a worker thread while the game waits here, and the menu shows the progress: the frame is
        // presented again and again until the worker is done. No 3D is built meanwhile, which keeps the record edits
        // safe, and the loader threads wait on the pipeline lock as they would for a synchronous apply.
        while (s_menu->isApplying()) {
            s_originalPresent(swapChain, syncInterval, flags);
            std::this_thread::sleep_for(applyFrameInterval);
            s_menu->renderApplyFrame(swapChain);
        }
    }

    return s_originalPresent(swapChain, syncInterval, flags);
}

HRESULT WINAPI Hooks::resizeBuffers(IDXGISwapChain* swapChain,
                                    UINT bufferCount,
                                    UINT width,
                                    UINT height,
                                    DXGI_FORMAT format,
                                    UINT flags)
{
    if (s_menu)
        s_menu->onResize();

    return s_originalResizeBuffers(swapChain, bufferCount, width, height, format, flags);
}

}
