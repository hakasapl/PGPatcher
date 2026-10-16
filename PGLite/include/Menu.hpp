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
        /// @brief Whether the row is selected; the row keeps it, so the selection moves with the mods.
        bool isSelected = false;
    };

    /// @brief One texture rule row, as edited.
    struct TextureRule {
        std::string path;
        int typeIndex = 0;
    };

    bool initialize(IDXGISwapChain* swapChain);
    bool createBackBufferView(IDXGISwapChain* swapChain);

    /**
     * @brief The engine's framebuffer render target when it is another texture than the swap chain's back buffer, or
     * nullptr. Community Shaders' frame generation composes the presented frame from the framebuffer, so the menu is
     * drawn into it as well; when the two are one texture, or the framebuffer was already composed into the back
     * buffer, the second draw is not needed or changes nothing.
     */
    [[nodiscard]] RE::BSGraphics::RenderTargetData* separateFrameTarget() const;
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

    /// @brief Gives Dear ImGui the keyboard's modifier state. Render thread, open and initialized.
    static void syncModifiers();

    /// @brief Copies the parameters and the mod list from the runtime into the working copy.
    void loadState();

    /// @brief Writes the settings and the mod rules, then starts the runtime's apply on the worker thread.
    void startApply(bool isEnabled);
    void finishApply();

    /**
     * @brief The mod list follows the applied mod manager settings: while the settings as edited are the applied
     * ones and the environment read its mod manager, the rows are that manager's mods, attributed by the mesh
     * reading pass. As soon as the type, the instance location or the order lock is edited the list goes, and it
     * comes back with the next Apply that reads a mod manager. The instance location's validity is told as it is
     * typed. Runs every frame; the work happens when the settings changed.
     */
    void rebuildModList();

    /// @brief The rows of the mod list from a mod manager: the mods with shaders or meshes, in priority order.
    void fillModRows(PGModManager& manager);

    /**
     * @brief Whether a row moves with a drag: the selection moves when the row the drag started on is part of it,
     * that row alone otherwise.
     *
     * @param row The row asked about.
     * @param draggedRow The row the drag started on.
     */
    [[nodiscard]] bool isMovedByDrag(std::size_t row,
                                     std::size_t draggedRow) const;

    /// @brief Draws the mods a drag moves into the drag source's tooltip; called inside BeginDragDropSource().
    void drawDragGhost(std::size_t draggedRow) const;

    /**
     * @brief Moves the mods of a drag to a gap between the rows, in their order; every other row keeps its order.
     *
     * @param draggedRow The row the drag started on.
     * @param gap The row the mods go before, the row count for the end of the list.
     * @return The new index of every row, by its index before the move.
     */
    std::vector<std::size_t> moveMods(std::size_t draggedRow,
                                      std::size_t gap);

    /**
     * @brief Scrolls the mod list while a drag holds the cursor near its top or bottom edge or past it, faster the
     * farther in. Called inside the table after its rows, where the current window is the list's scrolling child.
     *
     * @param rowsTop The top of the visible rows, below the frozen header.
     * @param rowsBottom The bottom of the visible rows.
     * @param rowHeight The height of one row.
     */
    void autoscrollModList(float rowsTop,
                           float rowsBottom,
                           float rowHeight);

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

    /**
     * @brief A panel in the top right corner: an image at the left, a line of text centered on it. It is drawn from
     * the sizes of this frame's text and image, so it never lags a frame behind a text that just changed, as an
     * auto-resized window does.
     *
     * @param image The image, skipped when it is not loaded.
     * @param imageHeight The height the image is drawn at.
     * @param text The formatted line.
     */
    void drawCornerPanel(const AnimatedImage& image,
                         float imageHeight,
                         const std::string& text) const;

    /// @brief Draws an editable list of strings with remove buttons and an add row.
    static void drawStringList(const char* id,
                               std::vector<std::string>& items,
                               std::string& newItem);

    /// @brief The width of a button with this label, so text fields can leave room for it.
    static float buttonWidth(const char* label);

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
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_backBuffer;
    std::chrono::steady_clock::time_point m_lastFrame;
    float m_mouseX = 0;
    float m_mouseY = 0;
    float m_uiScale = 1;
    AnimatedImage m_runningImage;
    AnimatedImage m_icon;
    ImVector<ImWchar> m_glyphRanges;
    bool m_shouldRebuildFonts = false;
    std::vector<PGTranslation::Language> m_languages;
    /// @brief The language code as edited; applied with Apply, like every other setting.
    std::string m_language;
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
    /// @brief The part of a pixel the mod list's autoscroll has not scrolled yet; scrolling moves whole pixels.
    float m_autoscrollCarry = 0;
    std::string m_modListSignature;
    std::string m_modListMessage;
    std::string m_mo2InstanceDir;
    /// @brief The validity of the instance location as typed, told under it.
    std::string m_instanceMessage;
    bool m_isInstanceValid = false;
    std::string m_newAllowEntry;
    std::string m_newBlockEntry;
    std::string m_newTexturePath;
    int m_newTextureType = 0;
    std::string m_applyMessage;
};

}
