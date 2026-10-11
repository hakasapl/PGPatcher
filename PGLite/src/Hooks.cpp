#include "PCH.h"

#include "Hooks.hpp"

#include "Menu.hpp"
#include "util/Logger.hpp"

#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

namespace PGLite {

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

    auto* const inputManager = RE::BSInputDeviceManager::GetSingleton();
    if (!inputManager) {
        Logger::error("The input manager is unavailable, the menu is disabled");
        return false;
    }

    s_menu = &menu;

    // The vtable of the swap chain class; the previous entries are called after the menu, so other overlays chain.
    REL::Relocation<std::uintptr_t> vtable {
        *reinterpret_cast<std::uintptr_t*>(swapChain) // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    };
    s_originalPresent = reinterpret_cast<decltype(&present)>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        vtable.write_vfunc(presentIndex, &present));
    s_originalResizeBuffers = reinterpret_cast<decltype(&resizeBuffers)>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        vtable.write_vfunc(resizeBuffersIndex, &resizeBuffers));

    inputManager->AddEventSink(&s_inputSink);

    Logger::debug("Menu hooks installed");
    return true;
}

RE::BSEventNotifyControl Hooks::InputSink::ProcessEvent(RE::InputEvent* const* events,
                                                        [[maybe_unused]] RE::BSTEventSource<RE::InputEvent*>* source)
{
    if (!events || !s_menu)
        return RE::BSEventNotifyControl::kContinue;

    for (const auto* event = *events; event; event = event->next)
        s_menu->handleInput(*event);

    return RE::BSEventNotifyControl::kContinue;
}

HRESULT WINAPI Hooks::present(IDXGISwapChain* swapChain,
                              UINT syncInterval,
                              UINT flags)
{
    if (s_menu)
        s_menu->render(swapChain);

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
