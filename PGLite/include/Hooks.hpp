#pragma once

#include <cstddef>
#include <cstdint>

namespace PGLite {

class Menu;

/**
 * @brief The seams of the GUI into the game (DESIGN.md section 4.10): the calls that draw the game's interface, after
 * which the menu is drawn, and the hand-off of the polled input events to the game, where the menu reads its input
 * and its hotkey and where the game is cut off from the input while the menu is open.
 *
 * The menu is drawn as SKSE Menu Framework draws its menus: right after MenuManager::DrawInterface returns, into the
 * render target the interface was drawn into, from both places the engine draws the interface (Main::Render, and the
 * frame of a loading screen). Community Shaders redirects that target at the start of DrawInterface, to the buffer its
 * HDR output and its frame generation compose the interface from, so the menu is composed like the game's own menus;
 * drawn into the swap chain's buffer at Present instead, it was drawn over a frame those had composed already. Both
 * calls are redirected through the SKSE trampoline, as is the input hand-off: the one call inside
 * BSInputDeviceManager::PollInputDevices that passes the frame's events to the input manager's event source, and so to
 * MenuControls, PlayerControls and every sink. While the menu is open the source receives an empty list, so a click in
 * the menu is no attack, the camera does not turn and no other plugin sees a hotkey. Community Shaders, Wheeler and
 * SKSE Menu Framework redirect some of the same calls, and the redirects chain in either order.
 */
class Hooks {
public:
    /**
     * @brief At plugin load: redirects the calls that draw the interface. Nothing runs them yet, so none is rewritten
     * while a thread executes it; they draw no menu until install() gives them one.
     */
    static void installEarly();

    /**
     * @brief Installs the input seam for a menu, at data load, once the renderer exists, and gives the interface draws
     * the menu.
     *
     * @return false if the renderer is unavailable, the interface draws could not be redirected or the input call site
     * is not the expected instruction (logged).
     */
    static bool install(Menu& menu);

    /// @brief Trampoline bytes the redirected calls need, allocated at plugin load with SKSE::AllocTrampoline.
    static constexpr std::size_t trampolineSize = 64;

private:
    /// @brief The redirected input dispatch: the menu reads the events, swallows them while it is open, and the
    /// engine's dispatch (or the hook of the plugin that redirected the call before PGLite) gets them otherwise. The
    /// list head is writable, as the engine passes it: hooks down the chain rewrite it to filter events.
    static void dispatchInput(RE::BSTEventSource<RE::InputEvent*>* source,
                              RE::InputEvent** events);

    /// @brief The redirected DrawInterface call of Main::Render.
    static void drawInterfaceInRender(RE::UI* ui);

    /// @brief The redirected DrawInterface call of the frame a loading screen renders.
    static void drawInterfaceInFrame(RE::UI* ui);

    /**
     * @brief Draws the interface, then the menu over it. While Apply runs, the thread stays here and shows the
     * progress: each frame is presented, and the next one is the kept game frame with the interface drawn again and
     * the menu over it, so every present follows one interface draw, as the frames of the game do.
     *
     * @param drawInterface The previous target of the redirected call: DrawInterface, or the hook of the plugin that
     * redirected the call before PGLite.
     * @param ui The interface, the argument of the call.
     */
    static void drawInterfaceAndMenu(void (*drawInterface)(RE::UI*),
                                     RE::UI* ui);

    /// @brief Presents a progress frame of Apply through the swap chain, with the game's present interval.
    static void presentApplyFrame();

    /**
     * @brief Redirects one DrawInterface call.
     *
     * @return The previous target of the call, or 0 if the site is not a call (logged).
     */
    static std::uintptr_t redirectDrawInterface(std::uintptr_t site,
                                                void (*thunk)(RE::UI*),
                                                const char* name);

    /// @brief Offset of the DrawInterface call inside Main::Render.
    static constexpr std::ptrdiff_t renderCallOffsetSE = 0x3AB;
    static constexpr std::ptrdiff_t renderCallOffsetAE = 0x371;
    static constexpr std::ptrdiff_t renderCallOffsetVR = 0x355;
    /// @brief Offset of the DrawInterface call inside the loading screen's frame: 1.6 has it where SE has it, 1.7 six
    /// bytes further.
    static constexpr std::ptrdiff_t frameCallOffsetSE = 0x19A;
    static constexpr std::ptrdiff_t frameCallOffsetAE17 = 0x1A0;
    static constexpr std::ptrdiff_t frameCallOffsetVR = 0x3FC;
    /// @brief Main::Render and the loading screen's frame in VR 1.4.15, by offset, like PollInputDevices below.
    static constexpr std::uint64_t renderVROffset = 0x5B8A20;
    static constexpr std::uint64_t frameVROffset = 0x648AE0;

    /// @brief Offset of the dispatch call inside BSInputDeviceManager::PollInputDevices: the same in SE and AE, six
    /// bytes further in VR, whose device loop tests one more field.
    static constexpr std::ptrdiff_t pollDispatchCallOffset = 0x7B;
    static constexpr std::ptrdiff_t pollDispatchCallOffsetVR = 0x81;
    /// @brief PollInputDevices in VR 1.4.15, by offset: its Address Library maps the SE ID to it, but the offset keeps
    /// the hook independent of that entry.
    static constexpr std::uint64_t pollInputDevicesVROffset = 0xC519E0;
    static constexpr std::uint8_t callOpcode = 0xE8;

    static inline REL::Relocation<decltype(&drawInterfaceInRender)> s_originalDrawInterfaceInRender;
    static inline REL::Relocation<decltype(&drawInterfaceInFrame)> s_originalDrawInterfaceInFrame;
    static inline REL::Relocation<decltype(&dispatchInput)> s_originalDispatch;
    static inline Menu* s_menu = nullptr;
};

}
