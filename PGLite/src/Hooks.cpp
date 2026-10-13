#include "PCH.h"

#include "Hooks.hpp"

#include "Menu.hpp"
#include "util/Logger.hpp"

#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <thread>
#include <tuple>
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

    // BSInputDeviceManager::PollInputDevices: Address Library IDs for SE and AE, the offset for VR. The dispatch call
    // sits at the same offset in SE and AE and six bytes further in VR, whose device loop tests one more field
    // (checked in the 1.5.97, 1.7.99 and VR databases and in the 1.7.104 executable: `mov rcx, rsi` and
    // `lea rdx, [rsp+40h]` load the source and the address of the event list head right before it). Only the opcode
    // is checked, because another plugin that redirected the call first has already replaced its target.
    const auto pollInputDevices = REL::VariantID(67315, 68617, pollInputDevicesVROffset).address();
    const auto site = pollInputDevices + (REL::Module::IsVR() ? pollDispatchCallOffsetVR : pollDispatchCallOffset);
    const auto opcode
        = *reinterpret_cast<const std::uint8_t*>(site); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (opcode != callOpcode) {
        Logger::error(
            "The input dispatch call site holds {:#x} instead of a call ({:#x}), the menu is disabled", opcode, site);
        return false;
    }

    // The vtable of the swap chain class; the previous entries are called after the menu, so other overlays chain.
    // They are read and stored before the entries are replaced: the loading-screen thread presents while this runs
    // (the data-load handler), and a present between the write and the store would call a null pointer. The stores
    // are ordered before the vtable writes by the memory model of x64, and the menu is set last, once both seams are
    // in place.
    REL::Relocation<std::uintptr_t> vtable {
        *reinterpret_cast<std::uintptr_t*>(swapChain) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    };
    const auto entry = [&vtable](std::size_t index) {
        return *reinterpret_cast<const std::uintptr_t*>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            vtable.address() + index * sizeof(std::uintptr_t));
    };
    s_originalPresent = reinterpret_cast<decltype(&present)>(
        entry(presentIndex)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    s_originalResizeBuffers
        = reinterpret_cast<decltype(&resizeBuffers)>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            entry(resizeBuffersIndex));
    std::ignore = vtable.write_vfunc(presentIndex, &present);
    std::ignore = vtable.write_vfunc(resizeBuffersIndex, &resizeBuffers);

    s_originalDispatch = SKSE::GetTrampoline().write_call<5>(site, &dispatchInput);
    s_menu = &menu;

    Logger::debug("Menu hooks installed (input dispatch call at {:#x} went to {:#x}, swap chain at {:#x})",
                  site,
                  s_originalDispatch.address(),
                  reinterpret_cast<std::uintptr_t>(swapChain)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    return true;
}

void Hooks::dispatchInput(RE::BSTEventSource<RE::InputEvent*>* source,
                          RE::InputEvent* const* events)
{
    if (events && s_menu)
        for (const auto* event = *events; event; event = event->next)
            s_menu->handleInput(*event);

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
    // A test present (occlusion check) presents nothing.
    if (!s_menu || (flags & DXGI_PRESENT_TEST))
        return s_originalPresent(swapChain, syncInterval, flags);

    s_menu->render(swapChain);

    if (s_menu->isApplying()) {
        // The thread parked below owns the game window and pumps no messages meanwhile; without this Windows would
        // replace the window with a ghost of its last image after a few seconds and hide the progress frames.
        static const bool isGhostingDisabled = [] {
            DisableProcessWindowsGhosting();
            return true;
        }();
        std::ignore = isGhostingDisabled;

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
