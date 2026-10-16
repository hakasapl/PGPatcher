#include "PCH.h"

#include "Hooks.hpp"

#include "Menu.hpp"
#include "util/Logger.hpp"

#include <chrono>
#include <cstdint>
#include <dxgi.h>
#include <thread>
#include <tuple>
#include <windows.h>

namespace PGLite {

namespace {

/// @brief The pause between two progress frames of Apply, on top of the swap chain's own pacing.
constexpr auto applyFrameInterval = std::chrono::milliseconds(10);

}

void Hooks::installEarly()
{
    // Main::Render and the frame of a loading screen are the only callers of MenuManager::DrawInterface: Address
    // Library IDs for SE and AE, offsets for VR, and the call inside each (checked in the 1.5.97, 1.7.104 and VR
    // 1.4.15 executables, `mov rcx, [UI singleton]` loads the argument right before it). 1.7 has the frame's call six
    // bytes further than 1.6, which has it where SE has it (as SKSE Menu Framework redirects it).
    const auto renderOffset = REL::Module::IsVR() ? renderCallOffsetVR
        : REL::Module::IsAE()                     ? renderCallOffsetAE
                                                  : renderCallOffsetSE;
    const bool isAE17 = REL::Module::IsAE() && REL::Module::get().version().minor() == 7;
    const auto frameOffset = REL::Module::IsVR() ? frameCallOffsetVR : isAE17 ? frameCallOffsetAE17 : frameCallOffsetSE;
    s_originalDrawInterfaceInRender = redirectDrawInterface(
        REL::VariantID(35556, 36555, renderVROffset).address() + renderOffset, &drawInterfaceInRender, "Main::Render");
    s_originalDrawInterfaceInFrame
        = redirectDrawInterface(REL::VariantID(38085, 39039, frameVROffset).address() + frameOffset,
                                &drawInterfaceInFrame,
                                "the loading screen frame");
}

std::uintptr_t Hooks::redirectDrawInterface(std::uintptr_t site,
                                            void (*thunk)(RE::UI*),
                                            const char* name)
{
    // Only the opcode is checked: another plugin that redirected the call first has already replaced its target.
    const auto opcode
        = *reinterpret_cast<const std::uint8_t*>(site); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    if (opcode != callOpcode) {
        Logger::error("The interface draw call of {} holds {:#x} instead of a call ({:#x})", name, opcode, site);
        return 0;
    }

    const auto previous = SKSE::GetTrampoline().write_call<5>(site, thunk);
    Logger::debug("The interface draw call of {} at {:#x} is redirected (it went to {:#x})", name, site, previous);
    return previous;
}

bool Hooks::install(Menu& menu)
{
    if (!RE::BSGraphics::Renderer::GetSingleton()) {
        Logger::error("The renderer is unavailable, the menu is disabled");
        return false;
    }

    // Main::Render draws every frame of the running game and of the main menu; the loading screen's frame only shows
    // the loading line.
    if (!s_originalDrawInterfaceInRender.address()) {
        Logger::error("The interface draw of Main::Render is not redirected, the menu is disabled");
        return false;
    }
    if (!s_originalDrawInterfaceInFrame.address())
        Logger::warn("The interface draw of the loading screen frame is not redirected, loading screens show no "
                     "loading line");

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

    // The menu is set last, once every seam is in place.
    s_originalDispatch = SKSE::GetTrampoline().write_call<5>(site, &dispatchInput);
    s_menu = &menu;

    Logger::debug(
        "Menu hooks installed (input dispatch call at {:#x} went to {:#x})", site, s_originalDispatch.address());
    return true;
}

void Hooks::dispatchInput(RE::BSTEventSource<RE::InputEvent*>* source,
                          RE::InputEvent** events)
{
    if (events && s_menu)
        for (const auto* event = *events; event; event = event->next)
            s_menu->handleInput(*event);

    if (s_menu && s_menu->isOpen()) {
        // The game sees no input while the menu is open. Keys still held resume when the devices poll them after the
        // close; the source is still called, as the engine does every frame. The empty list head has to be writable:
        // the input hooks of other plugins down the chain rewrite the head to filter events (SKSE Menu Framework
        // does), and a head in read-only memory crashed the game there.
        RE::InputEvent* noEvents = nullptr;
        s_originalDispatch(source, &noEvents);
        return;
    }

    s_originalDispatch(source, events);
}

void Hooks::drawInterfaceInRender(RE::UI* ui) { drawInterfaceAndMenu(s_originalDrawInterfaceInRender.get(), ui); }

void Hooks::drawInterfaceInFrame(RE::UI* ui) { drawInterfaceAndMenu(s_originalDrawInterfaceInFrame.get(), ui); }

void Hooks::drawInterfaceAndMenu(void (*drawInterface)(RE::UI*),
                                 RE::UI* ui)
{
    auto* const menu = s_menu;
    if (!menu) {
        drawInterface(ui);
        return;
    }

    // An Apply asked for in the previous frame starts before the interface is drawn, so the menu can keep the frame as
    // the game rendered it.
    menu->startPendingApply();
    drawInterface(ui);
    menu->render();
    if (!menu->isApplying())
        return;

    // The thread parked below owns the game window and pumps no messages meanwhile; without this Windows would replace
    // the window with a ghost of its last image after a few seconds and hide the progress frames.
    static const bool isGhostingDisabled = [] {
        DisableProcessWindowsGhosting();
        return true;
    }();
    std::ignore = isGhostingDisabled;

    // Apply runs on a worker thread while the game waits here, and the menu shows the progress. No 3D is built
    // meanwhile, which keeps the record edits safe, and the loader threads wait on the pipeline lock as they would for
    // a synchronous apply. Every present follows one interface draw, as in the game's own frames, which Community
    // Shaders relies on: it redirects the interface at the start of the draw and composes it at the present. The last
    // interface draw is presented by the game when this returns.
    while (menu->isApplying()) {
        presentApplyFrame();
        std::this_thread::sleep_for(applyFrameInterval);
        menu->restoreGameFrame();
        drawInterface(ui);
        menu->renderApplyFrame();
    }
}

void Hooks::presentApplyFrame()
{
    // Through the swap chain's vtable, so every overlay and compositor that detoured Present sees the frame.
    auto& data = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData();
    auto* const swapChain = reinterpret_cast<IDXGISwapChain*>( // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        data.renderWindows[0].swapChain);
    if (swapChain)
        swapChain->Present(data.presentInterval, 0);
}

}
