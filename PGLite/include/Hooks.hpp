#pragma once

#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

namespace PGLite {

class Menu;

/**
 * @brief The two seams of the GUI into the game (DESIGN.md section 4.10): the Present of the game's swap chain, where
 * the menu is drawn, and the engine's input event source, where the menu reads its input and its hotkey.
 *
 * No engine function is hooked: the swap chain is a COM object whose vtable entry is replaced (and the previous target
 * called, so other overlays chain), and the input is an event sink.
 */
class Hooks {
public:
    /**
     * @brief Installs both seams for a menu. Main thread, once the renderer and the input manager exist (data load).
     *
     * @return false if the swap chain or the input manager is unavailable (logged).
     */
    static bool install(Menu& menu);

private:
    class InputSink final : public RE::BSTEventSink<RE::InputEvent*> {
    public:
        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
                                              RE::BSTEventSource<RE::InputEvent*>* source) override;
    };

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

    static inline decltype(&present) s_originalPresent = nullptr;
    static inline decltype(&resizeBuffers) s_originalResizeBuffers = nullptr;
    static inline Menu* s_menu = nullptr;
    static inline InputSink s_inputSink;
};

}
