#pragma once

#include "Gif.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "PGTranslation.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <d3d11.h>
#include <dxgi.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

struct ImGuiContext;

namespace PGLite {

class Runtime;

/**
 * @brief The Dear ImGui menu (DESIGN.md section 4.10): the status of the plugin, the patcher settings, the allowed
 * record types, the mesh and texture rules, the mod manager and the mod list, the warnings, and Apply.
 *
 * Everything runs on the thread that presents: the loading-screen thread while the game loads, the main thread
 * afterwards, never both at once, and the input events are applied there. The menu keeps a working copy of the
 * parameters and of the mod list; Apply writes them to disk and hands them to the runtime.
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

    /**
     * @brief Queues one input event: the hotkey always, everything else while the menu is open. The engine dispatches
     * its input events on the main thread, which is not always the thread that presents, so they are applied to Dear
     * ImGui in render(), on the thread that draws.
     */
    void handleInput(const RE::InputEvent& event);

    [[nodiscard]] bool isOpen() const { return m_isOpen; }

    /// @brief True while Apply runs on its worker thread; Present keeps presenting progress frames then.
    [[nodiscard]] bool isApplying() const { return m_isApplying; }

    /**
     * @brief One progress frame of Apply: the game image kept when Apply started, the menu with the elapsed time and
     * the step the runtime is at. Called from Present while isApplying(); the frame that sees the worker done shows
     * the result and ends the sequence.
     */
    void renderApplyFrame(IDXGISwapChain* swapChain);

private:
    /// @brief One queued input event.
    struct InputRecord {
        enum class Kind : std::uint8_t { Hotkey, MouseMove, MouseButton, MouseWheel, Key, Character };

        Kind kind = Kind::Hotkey;
        bool isDown = false;
        std::uint32_t code = 0;
        float x = 0;
        float y = 0;
    };

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

    /// @brief One Dear ImGui frame into the back buffer. Render thread, initialized.
    void drawFrame(IDXGISwapChain* swapChain);

    /// @brief Ends the frame a failed draw left begun, so the next frame can begin.
    void endOpenFrame();
    bool capturePausedFrame(IDXGISwapChain* swapChain);
    void restorePausedFrame(IDXGISwapChain* swapChain);

    /// @brief Applies the queued input events to the menu and to Dear ImGui. Render thread.
    void drainInput();
    void applyInput(const InputRecord& record);

    /// @brief Copies the parameters and the mod list from the runtime into the working copy.
    void loadState();

    /// @brief Writes the settings and the mod rules, then starts the runtime's apply on the worker thread.
    void startApply(bool isEnabled);
    void finishApply();

    /**
     * @brief Builds the mod list from the mod manager settings as edited, when they changed. While they are the
     * applied ones the list is the environment's own mod manager, attributed by the mesh reading pass; otherwise the
     * mod manager is read, the mods get their shaders and meshes from the runtime's directory, and the shared
     * ordering numbers them, so the list is the one the next apply installs.
     */
    void rebuildModList();

    /// @brief The rows of the mod list from a mod manager: the mods with shaders or meshes, in priority order.
    void fillModRows(PGModManager& manager);

    void draw();
    void drawStatus();
    void drawPatchers();
    void drawRecordTypes();
    void drawMeshRules();
    void drawTextureRules();
    void drawConflictManager();
    void drawMessages();
    void drawSettings();

    /// @brief The font atlas with the glyphs of the loaded translation; a CJK font is merged when it needs one.
    void rebuildFonts();

    /// @brief Apply and Discard Changes, under every page.
    void drawApplyRow();

    /// @brief "Loading PGLite" in the top right corner while the runtime initializes (PGLite.ini bShowLoadingMessage).
    void drawLoadingMessage();

    /// @brief The welcome message in the top right corner until the menu was opened once (bShowWelcomeMessage).
    void drawWelcomeMessage();

    /// @brief The name of a key by its DirectInput scan code, as the keyboard layout calls it.
    [[nodiscard]] static std::string keyName(unsigned scanCode);

    /// @brief An image at the left of an overlay line, the text that follows centered on it.
    static void drawOverlayImage(const AnimatedImage& image,
                                 float height);

    /// @brief Draws an editable list of strings with remove buttons and an add row.
    static void drawStringList(const char* id,
                               std::vector<std::string>& items,
                               std::string& newItem);

    /// @brief The width of a button with this label, so text fields can leave room for it.
    static float buttonWidth(const char* label);

    /**
     * @brief Opens the folder dialog of the shell on its own thread; the game keeps running and draw() picks the
     * chosen folder up. One dialog at a time.
     */
    void browseForFolder(const char* title);
    void pollPickedFolder();

    // The clipboard of Dear ImGui, the Windows one.
    static const char* clipboardText(ImGuiContext* context);
    static void setClipboardText(ImGuiContext* context,
                                 const char* text);

    Runtime* m_runtime;
    bool m_isInitialized = false;
    bool m_didFailInitialization = false;
    std::atomic<bool> m_isOpen { false };
    bool m_didOpenMenu = false;
    bool m_isStateLoaded = false;
    /// @brief Whether a Dear ImGui frame is begun and not rendered yet, so a failed draw can end it.
    bool m_isFrameOpen = false;
    std::mutex m_inputMutex;
    std::vector<InputRecord> m_pendingInput;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_backBufferView;
    std::chrono::steady_clock::time_point m_lastFrame;
    float m_mouseX = 0;
    float m_mouseY = 0;
    HWND m_gameWindow = nullptr;
    float m_uiScale = 1;
    AnimatedImage m_runningImage;
    AnimatedImage m_icon;
    ImVector<ImWchar> m_glyphRanges;
    bool m_shouldRebuildFonts = false;
    std::vector<PGTranslation::Language> m_languages;
    /// @brief The language code as edited; applied with Apply, like every other setting.
    std::string m_language;
    std::mutex m_pickerMutex;
    bool m_isPickerOpen = false;
    std::optional<std::string> m_pickedFolder;
    static inline std::string s_clipboard;

    // Apply in progress.
    std::atomic<bool> m_isApplying { false };
    std::atomic<bool> m_isApplyDone { false };
    bool m_didApplySucceed = false;
    /// @brief Whether the running Apply enables PGLite.
    bool m_isApplyEnabling = true;
    std::thread m_applyThread;
    std::chrono::steady_clock::time_point m_applyStart;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_pausedFrame;

    // The working copy.
    PGParams m_params;
    std::vector<std::string> m_allowList;
    std::vector<std::string> m_blockList;
    std::vector<TextureRule> m_textureRules;
    std::vector<ModRow> m_mods;
    std::unique_ptr<PGModManager> m_previewModManager;
    /// @brief Whether the rows come from the environment's mod manager, the applied settings being the edited ones.
    bool m_isPreviewFromEnvironment = false;
    std::string m_modListSignature;
    std::string m_modListMessage;
    std::string m_mo2InstanceDir;
    std::string m_newAllowEntry;
    std::string m_newBlockEntry;
    std::string m_newTexturePath;
    int m_newTextureType = 0;
    std::string m_applyMessage;
};

}
