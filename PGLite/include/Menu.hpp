#pragma once

#include "PGParams.hpp"

#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <wrl/client.h>

namespace PGLite {

class Runtime;

/**
 * @brief The Dear ImGui menu (DESIGN.md section 4.10): the status of the plugin, the patcher settings, the allowed
 * record types, the mesh and texture rules, the mod manager and the mod list, the warnings, and Apply.
 *
 * Everything runs on the main thread: the input events arrive there and so does Present. The menu keeps a working
 * copy of the parameters and of the mod list; Apply writes them to disk and hands them to the runtime.
 */
class Menu {
public:
    explicit Menu(Runtime& runtime);
    ~Menu();
    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;
    Menu(Menu&&) = delete;
    Menu& operator=(Menu&&) = delete;

    /// @brief Draws the menu into the swap chain's back buffer when it is open. Called from Present.
    void render(IDXGISwapChain* swapChain);

    /// @brief Drops the back buffer view before the swap chain resizes its buffers.
    void onResize();

    /// @brief Reads one input event: the hotkey always, everything else while the menu is open.
    void handleInput(const RE::InputEvent& event);

    [[nodiscard]] bool isOpen() const { return m_isOpen; }

private:
    /// @brief One row of the mod list.
    struct ModRow {
        std::wstring name;
        std::string displayName;
        std::string shaders;
        bool isEnabled = false;
        bool areMeshesIgnored = false;
        bool hasMeshes = false;
        bool isNew = false;
    };

    /// @brief One texture rule row, as edited.
    struct TextureRule {
        std::string path;
        int typeIndex = 0;
    };

    bool initialize(IDXGISwapChain* swapChain);
    bool createBackBufferView(IDXGISwapChain* swapChain);
    void open();
    void close();
    void setGameControls(bool isEnabled);

    /// @brief Copies the parameters and the mod list from the runtime into the working copy.
    void loadState();
    void apply();

    void draw();
    void drawStatus();
    void drawPatchers();
    void drawRecordTypes();
    void drawMeshRules();
    void drawTextureRules();
    void drawModManager();
    void drawMods();
    void drawMessages();

    /// @brief Draws an editable list of strings with remove buttons and an add row.
    static void drawStringList(const char* id,
                               std::vector<std::string>& items,
                               std::string& newItem,
                               const char* hint);

    Runtime* m_runtime;
    bool m_isInitialized = false;
    bool m_didFailInitialization = false;
    bool m_isOpen = false;
    bool m_isStateLoaded = false;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_backBufferView;
    std::chrono::steady_clock::time_point m_lastFrame;
    float m_mouseX = 0;
    float m_mouseY = 0;

    // The working copy.
    PGParams m_params;
    std::vector<std::string> m_allowList;
    std::vector<std::string> m_blockList;
    std::vector<TextureRule> m_textureRules;
    std::vector<ModRow> m_mods;
    bool m_canReorderMods = false;
    std::string m_mo2InstanceDir;
    std::string m_detectedMO2Instance;
    std::string m_newAllowEntry;
    std::string m_newBlockEntry;
    std::string m_newTexturePath;
    int m_newTextureType = 0;
    std::string m_applyMessage;
};

}
