#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

namespace PGLite {

class Menu;

/**
 * @brief The two seams of the GUI into the game (DESIGN.md section 4.10): the Present of the game's swap chain, where
 * the menu is drawn, and the hand-off of the polled input events to the game, where the menu reads its input and its
 * hotkey and where the game is cut off from the input while the menu is open.
 *
 * The swap chain is a COM object whose vtable entries are replaced (and the previous targets called, so other overlays
 * chain). The input hand-off is the one call inside BSInputDeviceManager::PollInputDevices that passes the frame's
 * events to the input manager's event source, and so to MenuControls, PlayerControls and every sink. It is redirected
 * through the SKSE trampoline; while the menu is open the source receives an empty list, so a click in the menu is no
 * attack, the camera does not turn and no other plugin sees a hotkey. Community Shaders and Wheeler redirect the same
 * call, and the redirects chain in either order.
 */
class Hooks {
public:
    /**
     * @brief Installs both seams for a menu, at data load, once the renderer exists.
     *
     * @return false if the swap chain is unavailable or the input call site is not the expected instruction (logged).
     */
    static bool install(Menu& menu);

    /// @brief Trampoline bytes the input redirect needs, allocated at plugin load with SKSE::AllocTrampoline.
    static constexpr std::size_t trampolineSize = 64;

private:
    /// @brief The redirected input dispatch: the menu reads the events, swallows them while it is open, and the
    /// engine's dispatch (or the hook of the plugin that redirected the call before PGLite) gets them otherwise. The
    /// list head is writable, as the engine passes it: hooks down the chain rewrite it to filter events.
    static void dispatchInput(RE::BSTEventSource<RE::InputEvent*>* source,
                              RE::InputEvent** events);
    static HRESULT WINAPI present(IDXGISwapChain* swapChain,
                                  UINT syncInterval,
                                  UINT flags);
    static HRESULT WINAPI resizeBuffers(IDXGISwapChain* swapChain,
                                        UINT bufferCount,
                                        UINT width,
                                        UINT height,
                                        DXGI_FORMAT format,
                                        UINT flags);

    static constexpr std::size_t presentIndex = 8;
    static constexpr std::size_t resizeBuffersIndex = 13;

    /// @brief Offset of the dispatch call inside BSInputDeviceManager::PollInputDevices: the same in SE and AE, six
    /// bytes further in VR, whose device loop tests one more field.
    static constexpr std::ptrdiff_t pollDispatchCallOffset = 0x7B;
    static constexpr std::ptrdiff_t pollDispatchCallOffsetVR = 0x81;
    /// @brief PollInputDevices in VR 1.4.15, by offset: its Address Library maps the SE ID to it, but the offset keeps
    /// the hook independent of that entry.
    static constexpr std::uint64_t pollInputDevicesVROffset = 0xC519E0;
    static constexpr std::uint8_t callOpcode = 0xE8;

    static inline decltype(&present) s_originalPresent = nullptr;
    static inline decltype(&resizeBuffers) s_originalResizeBuffers = nullptr;
    static inline REL::Relocation<decltype(&dispatchInput)> s_originalDispatch;
    static inline Menu* s_menu = nullptr;
};

}
