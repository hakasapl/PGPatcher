#include "PCH.h"

#include "Menu.hpp"

#include "Environment.hpp"
#include "Icon.h"
#include "Logging.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "PGPlugin.hpp"
#include "PGTranslation.hpp"
#include "RunningGif.h"
#include "Runtime.hpp"
#include "Settings.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <dxgi.h>
#include <exception>
#include <filesystem>
#include <format>
#include <mutex>
#include <objbase.h>
#include <optional>
#include <shared_mutex>
#include <shobjidl.h>
#include <string>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

constexpr std::uint32_t escapeScanCode = 0x01;
constexpr std::uint32_t mouseWheelUp = 8;
constexpr std::uint32_t mouseWheelDown = 9;
constexpr unsigned mouseButtonCount = 5;
constexpr float minDeltaTime = 1.0F / 1000;
constexpr float referenceHeight = 1080;
constexpr float fontSize = 18;
/// @brief The first code point the Latin and Cyrillic font does not cover.
constexpr ImWchar firstCJKCodePoint = 0x2E80;

/// @brief A font of the Windows installation (Segoe UI, and Microsoft YaHei merged for the Chinese translation).
std::string fontPath(const wchar_t* fileName)
{
    std::array<wchar_t, MAX_PATH> windows = { };
    const auto length = GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size()));
    const std::filesystem::path folder = length && length < windows.size() ? std::filesystem::path(windows.data())
                                                                           : std::filesystem::path(L"C:\\Windows");
    return StringUtil::utf16toUTF8((folder / L"Fonts" / fileName).wstring());
}

/// @brief A string of the translation files (PGPatcher/translations), which both programs share.
std::string tr(const char* key) { return PGTranslation::tr(key); }

/// @brief The ImGui key of a DirectInput scan code, ImGuiKey_None for keys the menu does not use.
ImGuiKey imguiKeyOf(std::uint32_t scanCode)
{
    switch (scanCode) {
    case 0x01:
        return ImGuiKey_Escape;
    case 0x02:
        return ImGuiKey_1;
    case 0x03:
        return ImGuiKey_2;
    case 0x04:
        return ImGuiKey_3;
    case 0x05:
        return ImGuiKey_4;
    case 0x06:
        return ImGuiKey_5;
    case 0x07:
        return ImGuiKey_6;
    case 0x08:
        return ImGuiKey_7;
    case 0x09:
        return ImGuiKey_8;
    case 0x0A:
        return ImGuiKey_9;
    case 0x0B:
        return ImGuiKey_0;
    case 0x0C:
        return ImGuiKey_Minus;
    case 0x0D:
        return ImGuiKey_Equal;
    case 0x0E:
        return ImGuiKey_Backspace;
    case 0x0F:
        return ImGuiKey_Tab;
    case 0x10:
        return ImGuiKey_Q;
    case 0x11:
        return ImGuiKey_W;
    case 0x12:
        return ImGuiKey_E;
    case 0x13:
        return ImGuiKey_R;
    case 0x14:
        return ImGuiKey_T;
    case 0x15:
        return ImGuiKey_Y;
    case 0x16:
        return ImGuiKey_U;
    case 0x17:
        return ImGuiKey_I;
    case 0x18:
        return ImGuiKey_O;
    case 0x19:
        return ImGuiKey_P;
    case 0x1A:
        return ImGuiKey_LeftBracket;
    case 0x1B:
        return ImGuiKey_RightBracket;
    case 0x1C:
        return ImGuiKey_Enter;
    case 0x1D:
        return ImGuiKey_LeftCtrl;
    case 0x1E:
        return ImGuiKey_A;
    case 0x1F:
        return ImGuiKey_S;
    case 0x20:
        return ImGuiKey_D;
    case 0x21:
        return ImGuiKey_F;
    case 0x22:
        return ImGuiKey_G;
    case 0x23:
        return ImGuiKey_H;
    case 0x24:
        return ImGuiKey_J;
    case 0x25:
        return ImGuiKey_K;
    case 0x26:
        return ImGuiKey_L;
    case 0x27:
        return ImGuiKey_Semicolon;
    case 0x28:
        return ImGuiKey_Apostrophe;
    case 0x29:
        return ImGuiKey_GraveAccent;
    case 0x2A:
        return ImGuiKey_LeftShift;
    case 0x2B:
        return ImGuiKey_Backslash;
    case 0x2C:
        return ImGuiKey_Z;
    case 0x2D:
        return ImGuiKey_X;
    case 0x2E:
        return ImGuiKey_C;
    case 0x2F:
        return ImGuiKey_V;
    case 0x30:
        return ImGuiKey_B;
    case 0x31:
        return ImGuiKey_N;
    case 0x32:
        return ImGuiKey_M;
    case 0x33:
        return ImGuiKey_Comma;
    case 0x34:
        return ImGuiKey_Period;
    case 0x35:
        return ImGuiKey_Slash;
    case 0x36:
        return ImGuiKey_RightShift;
    case 0x37:
        return ImGuiKey_KeypadMultiply;
    case 0x38:
        return ImGuiKey_LeftAlt;
    case 0x39:
        return ImGuiKey_Space;
    case 0x3A:
        return ImGuiKey_CapsLock;
    case 0x47:
        return ImGuiKey_Keypad7;
    case 0x48:
        return ImGuiKey_Keypad8;
    case 0x49:
        return ImGuiKey_Keypad9;
    case 0x4A:
        return ImGuiKey_KeypadSubtract;
    case 0x4B:
        return ImGuiKey_Keypad4;
    case 0x4C:
        return ImGuiKey_Keypad5;
    case 0x4D:
        return ImGuiKey_Keypad6;
    case 0x4E:
        return ImGuiKey_KeypadAdd;
    case 0x4F:
        return ImGuiKey_Keypad1;
    case 0x50:
        return ImGuiKey_Keypad2;
    case 0x51:
        return ImGuiKey_Keypad3;
    case 0x52:
        return ImGuiKey_Keypad0;
    case 0x53:
        return ImGuiKey_KeypadDecimal;
    case 0x9C:
        return ImGuiKey_KeypadEnter;
    case 0x9D:
        return ImGuiKey_RightCtrl;
    case 0xB5:
        return ImGuiKey_KeypadDivide;
    case 0xB8:
        return ImGuiKey_RightAlt;
    case 0xC7:
        return ImGuiKey_Home;
    case 0xC8:
        return ImGuiKey_UpArrow;
    case 0xC9:
        return ImGuiKey_PageUp;
    case 0xCB:
        return ImGuiKey_LeftArrow;
    case 0xCD:
        return ImGuiKey_RightArrow;
    case 0xCF:
        return ImGuiKey_End;
    case 0xD0:
        return ImGuiKey_DownArrow;
    case 0xD1:
        return ImGuiKey_PageDown;
    case 0xD2:
        return ImGuiKey_Insert;
    case 0xD3:
        return ImGuiKey_Delete;
    default:
        return ImGuiKey_None;
    }
}

/// @brief The modifier an ImGui key is, ImGuiKey_None for a plain key.
ImGuiKey modifierOf(ImGuiKey key)
{
    switch (key) {
    case ImGuiKey_LeftCtrl:
    case ImGuiKey_RightCtrl:
        return ImGuiMod_Ctrl;
    case ImGuiKey_LeftShift:
    case ImGuiKey_RightShift:
        return ImGuiMod_Shift;
    case ImGuiKey_LeftAlt:
    case ImGuiKey_RightAlt:
        return ImGuiMod_Alt;
    default:
        return ImGuiKey_None;
    }
}

std::vector<std::string> toUTF8(const std::vector<std::wstring>& strings)
{
    std::vector<std::string> result;
    result.reserve(strings.size());
    for (const auto& str : strings)
        result.push_back(StringUtil::utf16toUTF8(str));
    return result;
}

std::vector<std::wstring> toUTF16(const std::vector<std::string>& strings)
{
    std::vector<std::wstring> result;
    result.reserve(strings.size());
    for (const auto& str : strings)
        if (!str.empty())
            result.push_back(StringUtil::utf8toUTF16(str));
    return result;
}

std::string shaderNames(const std::set<PGEnums::ShapeShader>& shaders)
{
    std::string names;
    for (const auto shader : shaders) {
        if (!names.empty())
            names += ", ";
        names += PGEnums::strFromShader(shader);
    }
    return names;
}

}

Menu::Menu(Runtime& runtime)
    : m_runtime(&runtime)
    , m_didOpenMenu(Settings::readDidOpenMenu())
    , m_lastFrame(std::chrono::steady_clock::now())
{
}

Menu::~Menu()
{
    if (m_applyThread.joinable())
        m_applyThread.join();

    if (m_isInitialized) {
        ImGui_ImplDX11_Shutdown();
        ImGui::DestroyContext();
    }
}

bool Menu::initialize(IDXGISwapChain* swapChain)
{
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&m_device))) || !m_device)
        return false;

    m_device->GetImmediateContext(&m_context);
    if (!m_context)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.MouseDrawCursor = true;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    DXGI_SWAP_CHAIN_DESC desc = { };
    float scale = 1;
    if (SUCCEEDED(swapChain->GetDesc(&desc)) && desc.BufferDesc.Height)
        scale = std::clamp(static_cast<float>(desc.BufferDesc.Height) / referenceHeight, 1.0F, 2.0F);
    m_uiScale = scale;

    rebuildFonts();
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::StyleColorsDark();

    // Ctrl+C and Ctrl+V in the text fields; the DX11 backend brings no clipboard of its own.
    auto& platformIO = ImGui::GetPlatformIO();
    platformIO.Platform_GetClipboardTextFn = &clipboardText;
    platformIO.Platform_SetClipboardTextFn = &setClipboardText;
    m_gameWindow = desc.OutputWindow;
    m_runningImage.load(EmbeddedImages::runningGif, EmbeddedImages::runningGifSize, *m_device.Get());
    m_icon.loadIcon(EmbeddedImages::icon, EmbeddedImages::iconSize, *m_device.Get());

    if (!ImGui_ImplDX11_Init(m_device.Get(), m_context.Get())) {
        ImGui::DestroyContext();
        return false;
    }

    Logger::debug(
        "Menu initialized Dear ImGui for a {} x {} back buffer", desc.BufferDesc.Width, desc.BufferDesc.Height);
    m_isInitialized = true;
    return true;
}

bool Menu::createBackBufferView(IDXGISwapChain* swapChain)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return false;

    return SUCCEEDED(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferView));
}

void Menu::render(IDXGISwapChain* swapChain)
{
    if (m_didFailInitialization || !swapChain)
        return;

    // The loading and welcome messages need a frame while the menu is closed.
    const auto& ini = m_runtime->ini();
    const bool shouldShowOverlay = !m_isOpen && m_runtime->isActive() && !m_runtime->disableReason()
        && ((ini.shouldShowLoadingMessage && !m_runtime->isReady())
            || (ini.shouldShowWelcomeMessage && !m_didOpenMenu && m_runtime->isReady()));

    try {
        if (!m_isInitialized && (m_isOpen || shouldShowOverlay) && !initialize(swapChain)) {
            m_didFailInitialization = true;
            Logger::error("The menu could not initialize Dear ImGui on the game's device");
            m_isOpen = false;
            return;
        }

        // The hotkey and, while open, the mouse and keys queued by the input threads.
        drainInput();
        if (!m_isOpen && !shouldShowOverlay)
            return;
        if (!m_isInitialized && !initialize(swapChain)) {
            m_didFailInitialization = true;
            Logger::error("The menu could not initialize Dear ImGui on the game's device");
            m_isOpen = false;
            return;
        }

        if (!m_backBufferView && !createBackBufferView(swapChain))
            return;

        drawFrame(swapChain);
    } catch (const std::exception& e) {
        Logger::error("The menu failed to draw: {}", e.what());
        endOpenFrame();
        if (!m_isApplying)
            close();
    }
}

void Menu::endOpenFrame()
{
    if (!m_isFrameOpen)
        return;

    ImGui::EndFrame();
    m_isFrameOpen = false;
}

void Menu::drawFrame(IDXGISwapChain* swapChain)
{
    // A language change rebuilds the atlas between frames, never inside one.
    if (m_shouldRebuildFonts) {
        m_shouldRebuildFonts = false;
        rebuildFonts();
    }

    DXGI_SWAP_CHAIN_DESC desc = { };
    if (FAILED(swapChain->GetDesc(&desc)))
        return;

    const auto now = std::chrono::steady_clock::now();
    const auto delta = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;

    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width), static_cast<float>(desc.BufferDesc.Height));
    io.DeltaTime = std::max(delta, minDeltaTime);

    ImGui_ImplDX11_NewFrame();
    ImGui::NewFrame();
    m_isFrameOpen = true;
    draw();
    ImGui::Render();
    m_isFrameOpen = false;

    // Apply started in this frame: keep the game image before the menu is drawn over it, for the progress frames.
    if (m_isApplying && !m_pausedFrame && !capturePausedFrame(swapChain))
        Logger::warn("The game image could not be kept for the progress frames of Apply");

    // Draw over whatever the game left in the back buffer, then give the context its targets back.
    ID3D11RenderTargetView* previousTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = { };
    ID3D11DepthStencilView* previousDepth = nullptr;
    m_context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previousTargets, &previousDepth);

    ID3D11RenderTargetView* const target = m_backBufferView.Get();
    m_context->OMSetRenderTargets(1, &target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    m_context->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, previousTargets, previousDepth);
    for (auto* const previous : previousTargets)
        if (previous)
            previous->Release();
    if (previousDepth)
        previousDepth->Release();
}

bool Menu::capturePausedFrame(IDXGISwapChain* swapChain)
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return false;

    D3D11_TEXTURE2D_DESC desc = { };
    backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags = 0;
    if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_pausedFrame)) || !m_pausedFrame)
        return false;

    m_context->CopyResource(m_pausedFrame.Get(), backBuffer.Get());
    return true;
}

void Menu::restorePausedFrame(IDXGISwapChain* swapChain)
{
    if (!m_pausedFrame)
        return;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer)
        m_context->CopyResource(backBuffer.Get(), m_pausedFrame.Get());
}

void Menu::renderApplyFrame(IDXGISwapChain* swapChain)
{
    const bool isDone = m_isApplyDone;
    if (isDone)
        finishApply();

    try {
        if (m_isInitialized && swapChain && (m_backBufferView || createBackBufferView(swapChain))) {
            drainInput();
            restorePausedFrame(swapChain);
            drawFrame(swapChain);
        }
    } catch (const std::exception& e) {
        Logger::error("The menu failed to draw a progress frame: {}", e.what());
        endOpenFrame();
    }

    if (isDone) {
        m_pausedFrame.Reset();
        m_isApplying = false;
    }
}

void Menu::onResize()
{
    m_backBufferView.Reset();
    if (m_isInitialized)
        ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Menu::handleInput(const RE::InputEvent& event)
{
    const auto* const button = event.AsButtonEvent();
    const bool isKeyboard = event.GetDevice() == RE::INPUT_DEVICE::kKeyboard;

    // The hotkey, open or closed.
    if (button && isKeyboard && button->GetIDCode() == m_runtime->ini().menuKey && button->IsDown()) {
        const std::scoped_lock lock(m_inputMutex);
        m_pendingInput.push_back({ .kind = InputRecord::Kind::Hotkey });
        return;
    }

    if (!m_isOpen)
        return;

    InputRecord record;
    switch (event.GetEventType()) {
    case RE::INPUT_EVENT_TYPE::kMouseMove: {
        const auto* const move = static_cast<const RE::MouseMoveEvent*>(&event);
        record.kind = InputRecord::Kind::MouseMove;
        record.x = static_cast<float>(move->mouseInputX);
        record.y = static_cast<float>(move->mouseInputY);
        break;
    }
    case RE::INPUT_EVENT_TYPE::kChar: {
        const auto* const character = static_cast<const RE::CharEvent*>(&event);
        record.kind = InputRecord::Kind::Character;
        record.code = character->keyCode;
        break;
    }
    case RE::INPUT_EVENT_TYPE::kButton: {
        if (!button || (!button->IsDown() && !button->IsUp()))
            return;

        record.isDown = button->IsDown();
        record.code = button->GetIDCode();
        if (event.GetDevice() == RE::INPUT_DEVICE::kMouse) {
            if (record.code == mouseWheelUp || record.code == mouseWheelDown) {
                if (!record.isDown)
                    return;

                record.kind = InputRecord::Kind::MouseWheel;
            } else if (record.code < mouseButtonCount) {
                record.kind = InputRecord::Kind::MouseButton;
            } else {
                return;
            }
            break;
        }

        if (!isKeyboard)
            return;

        record.kind = InputRecord::Kind::Key;
        break;
    }
    default:
        return;
    }

    const std::scoped_lock lock(m_inputMutex);
    m_pendingInput.push_back(record);
}

void Menu::drainInput()
{
    std::vector<InputRecord> records;
    {
        const std::scoped_lock lock(m_inputMutex);
        records.swap(m_pendingInput);
    }

    for (const auto& record : records)
        applyInput(record);
}

void Menu::applyInput(const InputRecord& record)
{
    // While Apply runs the menu stays open and only follows the mouse.
    if (m_isApplying && record.kind != InputRecord::Kind::MouseMove)
        return;

    if (record.kind == InputRecord::Kind::Hotkey) {
        Logger::debug("Menu hotkey pressed, the menu is {}", m_isOpen ? "closing" : "opening");
        if (m_isOpen)
            close();
        else
            open();
        return;
    }

    if (!m_isOpen || !m_isInitialized)
        return;

    auto& io = ImGui::GetIO();
    switch (record.kind) {
    case InputRecord::Kind::MouseMove:
        m_mouseX = std::clamp(m_mouseX + record.x, 0.0F, io.DisplaySize.x);
        m_mouseY = std::clamp(m_mouseY + record.y, 0.0F, io.DisplaySize.y);
        io.AddMousePosEvent(m_mouseX, m_mouseY);
        break;
    case InputRecord::Kind::Character:
        io.AddInputCharacter(record.code);
        break;
    case InputRecord::Kind::MouseWheel:
        io.AddMouseWheelEvent(0, record.code == mouseWheelUp ? 1 : -1);
        break;
    case InputRecord::Kind::MouseButton:
        io.AddMouseButtonEvent(static_cast<int>(record.code), record.isDown);
        break;
    case InputRecord::Kind::Key: {
        if (record.isDown && record.code == escapeScanCode) {
            close();
            break;
        }

        const auto key = imguiKeyOf(record.code);
        if (key == ImGuiKey_None)
            break;

        if (const auto modifier = modifierOf(key); modifier != ImGuiKey_None)
            io.AddKeyEvent(modifier, record.isDown);
        io.AddKeyEvent(key, record.isDown);
        break;
    }
    default:
        break;
    }
}

void Menu::open()
{
    // A disabled PGLite has no menu; the notice at data load said why. Before the initialization is done the plan and
    // the environment are still being built, and the status page reads both.
    if (m_runtime->disableReason() || !m_runtime->isReady())
        return;

    m_isOpen = true;
    m_isStateLoaded = false;
    m_applyMessage.clear();
    if (!m_didOpenMenu) {
        // The welcome message has done its job.
        m_didOpenMenu = true;
        std::ignore = Settings::writeDidOpenMenu();
    }
    m_lastFrame = std::chrono::steady_clock::now();
    if (m_isInitialized) {
        auto& io = ImGui::GetIO();
        io.ClearInputKeys();
        io.ClearInputMouse();
        m_mouseX = io.DisplaySize.x / 2;
        m_mouseY = io.DisplaySize.y / 2;
        io.AddMousePosEvent(m_mouseX, m_mouseY);
    }
}

void Menu::close() { m_isOpen = false; }

void Menu::loadState()
{
    m_params = m_runtime->params();
    m_allowList = toUTF8(m_params.processing.allowList);
    m_blockList = toUTF8(m_params.processing.blockList);

    m_textureRules.clear();
    const auto typeNames = PGEnums::texTypesStr();
    for (const auto& [path, type] : m_params.processing.textureMaps) {
        const auto name = PGEnums::strFromTexType(type);
        const auto it = std::ranges::find(typeNames, name);
        m_textureRules.push_back({ .path = StringUtil::utf16toUTF8(path),
                                   .typeIndex = it == typeNames.end() ? 0 : static_cast<int>(it - typeNames.begin()) });
    }

    m_mo2InstanceDir = StringUtil::utf16toUTF8(m_params.modManager.mo2InstanceDir.wstring());

    m_languages = PGTranslation::availableLanguages();

    // The mod list follows the settings as edited (rebuildModList).
    m_mods.clear();
    m_previewModManager.reset();
    m_modListSignature.clear();
    m_modListMessage.clear();

    m_isStateLoaded = true;
}

void Menu::rebuildModList()
{
    const auto type = m_params.modManager.type;
    const bool isMO2 = type == PGModManager::ModManagerType::ModOrganizer2;
    const auto signature = std::format(
        "{}|{}|{}", static_cast<int>(type), m_mo2InstanceDir, m_params.modManager.shouldUseMO2LooseFileOrder);
    if (signature == m_modListSignature)
        return;

    m_modListSignature = signature;
    m_mods.clear();
    m_previewModManager.reset();
    m_isPreviewFromEnvironment = false;
    m_modListMessage.clear();
    if (type == PGModManager::ModManagerType::None) {
        m_modListMessage = tr("pglite.conflictManager.noModManager");
        return;
    }

    // The settings as applied: the environment's own mod manager is the list the session runs with, attributed by
    // the mesh reading pass itself, and costs no walk of the mod folders.
    auto* const environment = m_runtime->environment();
    const auto applied = m_runtime->params().modManager;
    const bool isApplied = environment && environment->modManager() && type == applied.type
        && std::filesystem::path(StringUtil::utf8toUTF16(m_mo2InstanceDir)) == applied.mo2InstanceDir
        && m_params.modManager.shouldUseMO2LooseFileOrder == applied.shouldUseMO2LooseFileOrder;
    if (isApplied) {
        fillModRows(*environment->modManager());
        m_isPreviewFromEnvironment = true;
        return;
    }

    auto manager = std::make_unique<PGModManager>(type);
    try {
        nlohmann::json rules;
        if (Settings::readModRules(rules))
            manager->loadJSON(rules);

        if (isMO2) {
            // The checks of the offline launcher, with its messages.
            const std::filesystem::path dir = StringUtil::utf8toUTF16(m_mo2InstanceDir);
            std::error_code ec;
            if (dir.empty()) {
                m_modListMessage = tr("launcher.validation.mo2InstanceRequired");
                return;
            }
            if (!std::filesystem::exists(dir, ec)) {
                m_modListMessage = tr("launcher.validation.mo2InstanceMissing");
                return;
            }
            if (!PGModManager::isValidMO2InstanceDir(dir)) {
                m_modListMessage = tr("launcher.validation.mo2InstanceInvalid");
                return;
            }
            manager->populateModFileMapMO2(dir, { });
        } else {
            manager->populateModFileMapVortex(Settings::dataFolder());
        }

        auto* const environment = m_runtime->environment();
        if (environment && environment->directory())
            environment->directory()->attributeMods(*manager);
        manager->updateStateFromModlist(m_params.modManager.shouldUseMO2LooseFileOrder);
    } catch (const std::exception& e) {
        Logger::error("The mod list could not be built: {}", e.what());
        m_modListMessage = tr("pglite.conflictManager.couldNotBuild");
        const auto placeholder = m_modListMessage.find("%s");
        if (placeholder != std::string::npos)
            m_modListMessage.replace(placeholder, 2, e.what());
        return;
    }

    fillModRows(*manager);
    m_previewModManager = std::move(manager);
}

void Menu::fillModRows(PGModManager& manager)
{
    for (const auto& mod : manager.modsByPriority()) {
        const std::shared_lock lock(mod->mutex);
        if (mod->shaders.empty() && !mod->hasMeshes)
            continue;

        m_mods.push_back({ .name = mod->name,
                           .displayName = StringUtil::utf16toUTF8(mod->name),
                           .shaders = shaderNames(mod->shaders),
                           .isEnabled = mod->isEnabled,
                           .areMeshesIgnored = mod->areMeshesIgnored,
                           .hasMeshes = mod->hasMeshes,
                           .isNew = mod->isNew });
    }
}

void Menu::startApply(bool isEnabled)
{
    // The loading-screen thread presents during a load, and an Apply started from its frame would park that thread
    // while the main thread goes on loading records PGLite is about to edit.
    if (auto* const ui = RE::UI::GetSingleton(); ui && ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME)) {
        m_applyMessage = tr("pglite.apply.loading");
        return;
    }

    // The file holds every parameter with its full lists, as the offline program writes it.
    PGParams params = m_params;
    params.processing.allowList = toUTF16(m_allowList);
    params.processing.blockList = toUTF16(m_blockList);
    params.processing.textureMaps.clear();
    const auto typeNames = PGEnums::texTypesStr();
    for (const auto& rule : m_textureRules) {
        if (rule.path.empty() || rule.typeIndex < 0 || rule.typeIndex >= static_cast<int>(typeNames.size()))
            continue;

        params.processing.textureMaps.emplace_back(StringUtil::utf8toUTF16(rule.path),
                                                   PGEnums::texTypeFromStr(typeNames.at(rule.typeIndex)));
    }
    params.modManager.mo2InstanceDir = StringUtil::utf8toUTF16(m_mo2InstanceDir);

    // The lists as the next session reads them from the file (forward slashes become backslashes), so a rule typed
    // with forward slashes matches from this Apply on and not from the next start.
    {
        nlohmann::json normalized;
        params.writeJSON(normalized);
        PGParams::replaceForwardSlashes(normalized);
        PGParams normalizedParams;
        normalizedParams.readJSON(normalized);
        params = normalizedParams;
    }

    // What the offline launcher refuses, the menu refuses: the problems are shown and nothing is written.
    const auto problems = params.validate();
    if (!problems.empty()) {
        m_applyMessage.clear();
        for (const auto& problem : problems) {
            if (!m_applyMessage.empty())
                m_applyMessage += '\n';
            m_applyMessage += PGTranslation::trFormat(problem.key, StringUtil::utf16toUTF8(problem.item));
        }
        return;
    }

    if (!Settings::writeParams(params) || !Settings::writeIsEnabled(isEnabled)) {
        m_applyMessage = tr("pglite.apply.settingsNotWritten");
        return;
    }

    // The mod rules: the displayed mods as ordered and set here, every other mod as it was.
    PGModManager* rulesSource = m_previewModManager.get();
    if (m_isPreviewFromEnvironment && m_runtime->environment())
        rulesSource = m_runtime->environment()->modManager();
    if (rulesSource) {
        auto rules = rulesSource->json();
        int priority = static_cast<int>(std::ranges::count_if(m_mods, [](const ModRow& row) { return row.isEnabled; }));
        for (const auto& row : m_mods) {
            auto& rule = rules[row.displayName];
            rule["priority"] = row.isEnabled ? priority-- : -1;
            rule["enabled"] = row.isEnabled;
            rule["meshesignored"] = row.areMeshesIgnored;
        }

        if (!Settings::writeModRules(rules)) {
            m_applyMessage = tr("pglite.apply.modRulesNotWritten");
            return;
        }
    }

    // The runtime's apply runs on a worker thread; Present shows the progress until it is done (renderApplyFrame).
    m_applyMessage.clear();
    m_isApplyEnabling = isEnabled;
    m_isApplyDone = false;
    m_didApplySucceed = false;
    m_applyStart = std::chrono::steady_clock::now();
    m_isApplying = true;
    m_applyThread = std::thread([this, params, isEnabled] {
        m_didApplySucceed = m_runtime->apply(params, isEnabled);
        m_isApplyDone = true;
    });
}

void Menu::finishApply()
{
    if (m_applyThread.joinable())
        m_applyThread.join();

    const auto seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_applyStart).count();
    if (!m_didApplySucceed) {
        m_applyMessage = tr("pglite.apply.failed");
    } else if (!m_isApplyEnabling) {
        // The Apply row says so for as long as PGLite is disabled.
        m_applyMessage.clear();
    } else {
        // The string carries a printf placeholder for the seconds, as every translated string does.
        const auto text = tr("pglite.apply.applied");
        std::vector<char> buffer(text.size() + 32);
        std::snprintf(buffer.data(), buffer.size(), text.c_str(), seconds); // NOLINT(cppcoreguidelines-pro-type-vararg)
        m_applyMessage = buffer.data();
    }
    m_isStateLoaded = false;
}

void Menu::draw()
{
    drawLoadingMessage();
    drawWelcomeMessage();
    if (!m_isOpen)
        return;

    if (!m_isStateLoaded)
        loadState();
    pollPickedFolder();

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.4F, io.DisplaySize.y * 0.5F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.3F, io.DisplaySize.y * 0.25F), ImGuiCond_FirstUseEver);

    // No close button while Apply runs; the hotkey and Escape are ignored then too (applyInput).
    // The version in the title, as the offline launcher window, and its icon drawn into the title bar over the room
    // leading spaces leave it; the part after ### keeps the window's identity.
    const auto iconSize = ImGui::GetFontSize();
    const auto& style = ImGui::GetStyle();
    const auto spaceCount = m_icon.isLoaded()
        ? static_cast<std::size_t>(std::ceil((iconSize + style.ItemInnerSpacing.x) / ImGui::CalcTextSize(" ").x))
        : 0;
    const auto title = std::format("{}PGLite {}###PGLite", std::string(spaceCount, ' '), PG_FULL_VERSION);
    bool isOpen = true;
    if (!ImGui::Begin(title.c_str(), m_isApplying ? nullptr : &isOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (m_icon.isLoaded()) {
        const auto position = ImGui::GetWindowPos();
        const ImVec2 iconMin(position.x + style.FramePadding.x, position.y + style.FramePadding.y);
        const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
        auto* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(
            position, ImVec2(position.x + ImGui::GetWindowWidth(), position.y + ImGui::GetFrameHeight()), false);
        drawList->AddImage(m_icon.frame(), iconMin, iconMax);
        drawList->PopClipRect();
    }

    if (m_isApplying) {
        const auto seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_applyStart).count();
        const auto* const stage = m_runtime->stage();
        ImGui::TextColored(ImVec4(1, 0.85F, 0.4F, 1),
                           tr("pglite.status.applying").c_str(),
                           tr(stage ? stage : "pglite.status.working").c_str(),
                           seconds);
        ImGui::Separator();
    }

    ImGui::BeginDisabled(m_isApplying);
    // The pages fill the window above the Apply row, which every page shares.
    const auto applyRowHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("pages", ImVec2(0, -applyRowHeight)) && ImGui::BeginTabBar("pages")) {
        const auto page = [](const char* key, auto&& draw) {
            if (ImGui::BeginTabItem(tr(key).c_str())) {
                draw();
                ImGui::EndTabItem();
            }
        };
        page("pglite.tabs.status", [this] { drawStatus(); });
        page("pglite.tabs.patchers", [this] { drawPatchers(); });
        page("conflictManager.title", [this] { drawConflictManager(); });
        page("launcher.processing.allowedRecordTypes", [this] { drawRecordTypes(); });
        page("pglite.tabs.meshRules", [this] { drawMeshRules(); });
        page("launcher.processing.textureRules", [this] { drawTextureRules(); });
        page("pglite.tabs.messages", [this] { drawMessages(); });
        page("settings.title", [this] { drawSettings(); });
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    drawApplyRow();
    ImGui::EndDisabled();
    ImGui::End();

    if (!isOpen)
        close();
}

void Menu::drawApplyRow()
{
    ImGui::Separator();
    const bool canApply = m_runtime->isActive() && m_runtime->isReady() && !m_runtime->disableReason();
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button(tr("conflictManager.buttons.apply").c_str()))
        startApply(m_runtime->isEnabled());
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(tr("conflictManager.buttons.discardChanges").c_str()))
        m_isStateLoaded = false;
    // A failure shows whatever the state; the disabled note fills the row otherwise.
    const auto message
        = !m_applyMessage.empty() || m_runtime->isEnabled() ? m_applyMessage : tr("pglite.apply.disabled");
    if (!message.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", message.c_str());
    }
}

void Menu::drawLoadingMessage()
{
    if (!m_runtime->ini().shouldShowLoadingMessage || !m_runtime->isActive() || m_runtime->isReady()
        || m_runtime->disableReason())
        return;

    const auto& io = ImGui::GetIO();
    const auto margin = 16 * m_uiScale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, margin), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.6F);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##loading", nullptr, flags)) {
        const auto* const stage = m_runtime->stage();
        const auto [done, total] = m_runtime->progress();
        const auto stageText = tr(stage ? stage : "pglite.status.starting");
        if (m_runningImage.isLoaded())
            drawOverlayImage(m_runningImage, m_runningImage.size().y * m_uiScale * 0.5F);
        if (total)
            ImGui::Text(tr("pglite.status.loadingProgress").c_str(), stageText.c_str(), done, total);
        else
            ImGui::Text(tr("pglite.status.loading").c_str(), stageText.c_str());
    }
    ImGui::End();
}

void Menu::drawOverlayImage(const AnimatedImage& image,
                            float height)
{
    // The image at the left of the line, the text that follows centered on it.
    const auto size = ImVec2(height * image.size().x / image.size().y, height);
    const auto lineStart = ImGui::GetCursorPosY();
    ImGui::Image(image.frame(), size);
    ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 2);
    ImGui::SetCursorPosY(lineStart + (size.y - ImGui::GetTextLineHeight()) / 2);
}

void Menu::drawWelcomeMessage()
{
    if (!m_runtime->ini().shouldShowWelcomeMessage || m_didOpenMenu || !m_runtime->isActive() || !m_runtime->isReady()
        || m_runtime->disableReason())
        return;

    const auto& io = ImGui::GetIO();
    const auto margin = 16 * m_uiScale;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - margin, margin), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.6F);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##welcome", nullptr, flags)) {
        if (m_icon.isLoaded())
            drawOverlayImage(m_icon, ImGui::GetFontSize() * 2);
        ImGui::Text(tr("pglite.status.welcome").c_str(), keyName(m_runtime->ini().menuKey).c_str());
    }
    ImGui::End();
}

std::string Menu::keyName(unsigned scanCode)
{
    // DirectInput scan codes are the keyboard's, bit 7 marking an extended key, which GetKeyNameText takes in bit 24.
    const auto parameter = static_cast<LONG>(((scanCode & 0x7FU) << 16U) | ((scanCode & 0x80U) ? (1U << 24U) : 0U));
    std::array<wchar_t, 64> name = { };
    if (GetKeyNameTextW(parameter, name.data(), static_cast<int>(name.size())) > 0)
        return StringUtil::utf16toUTF8(name.data());

    return std::format("{}", scanCode);
}

void Menu::drawStatus()
{
    if (m_isApplying) {
        // The runtime's plan and environment are being replaced; the counters return with the result.
        ImGui::TextUnformatted(tr("pglite.status.applyingShort").c_str());
        return;
    }

    const auto status = m_runtime->status();
    if (!status.isActive) {
        ImGui::TextUnformatted(tr("pglite.status.waitingForData").c_str());
    } else if (!status.isReady) {
        ImGui::Text(tr("pglite.status.initializing").c_str(),
                    tr(status.stage ? status.stage : "pglite.status.initializingDefault").c_str());
    } else {
        const auto color = status.isEnabled ? ImVec4(0.5F, 1, 0.5F, 1) : ImVec4(1, 0.5F, 0.3F, 1);
        const auto text = tr(status.isEnabled ? "pglite.status.active" : "pglite.status.disabled");
        if (m_runningImage.isLoaded()) {
            // The running animation of the offline launcher at half size at the left of the status, frozen while
            // PGLite is disabled, the text centered on it and the block padded all around.
            const auto& style = ImGui::GetStyle();
            const auto padding = style.ItemSpacing.y * 2;
            const auto imageSize
                = ImVec2(m_runningImage.size().x * m_uiScale * 0.5F, m_runningImage.size().y * m_uiScale * 0.5F);
            ImGui::Dummy(ImVec2(0, padding));
            ImGui::Indent(padding);
            const auto lineStart = ImGui::GetCursorPosY();
            ImGui::Image(m_runningImage.frame(status.isEnabled), imageSize);
            ImGui::SameLine(0, style.ItemSpacing.x * 2);
            ImGui::SetCursorPosY(lineStart + (imageSize.y - ImGui::GetTextLineHeight()) / 2);
            ImGui::TextColored(color, "%s", text.c_str());
            ImGui::Unindent(padding);
            ImGui::Dummy(ImVec2(0, padding));
        } else {
            ImGui::TextColored(color, "%s", text.c_str());
        }
    }

    if (status.isActive && status.isReady && !status.disableReason) {
        if (ImGui::Button(tr(status.isEnabled ? "pglite.status.disable" : "pglite.status.enable").c_str()))
            startApply(!status.isEnabled);
    }

    ImGui::Separator();
    ImGui::Text(tr("pglite.status.uses").c_str(), status.uses, status.usedMeshes);
    ImGui::Text(tr("pglite.status.families").c_str(), status.families, status.facegenFamilies, status.patchedFamilies);
    ImGui::Text(tr("pglite.status.variants").c_str(), status.variants);
    ImGui::Text(tr("pglite.status.streams").c_str(), status.streams);
    ImGui::Text(tr("pglite.status.textureSets").c_str(), status.textureSets);
    ImGui::Text(tr("pglite.status.textures").c_str(),
                status.texturesFromCache,
                status.texturesGenerated,
                status.texturesFailed);
    if (!status.mo2Instance.empty())
        ImGui::Text(tr("pglite.status.mo2Instance").c_str(),
                    StringUtil::utf16toUTF8(status.mo2Instance.wstring()).c_str());
}

void Menu::drawPatchers()
{
    ImGui::SeparatorText(tr("launcher.prePatchers.title").c_str());
    ImGui::Checkbox(tr("launcher.prePatchers.fixMeshLighting.label").c_str(),
                    &m_params.prePatcher.isFixMeshLightingEnabled);

    ImGui::SeparatorText(tr("launcher.shaderPatchers.title").c_str());
    ImGui::Checkbox(tr("launcher.shaderPatchers.parallax.label").c_str(), &m_params.shaderPatcher.isParallaxEnabled);
    ImGui::Checkbox(tr("launcher.shaderPatchers.complexMaterial.label").c_str(),
                    &m_params.shaderPatcher.isComplexMaterialEnabled);
    ImGui::Checkbox(tr("launcher.shaderPatchers.truePBR.label").c_str(), &m_params.shaderPatcher.isTruePBREnabled);

    ImGui::SeparatorText(tr("launcher.shaderTransforms.title").c_str());
    ImGui::Checkbox(tr("launcher.shaderTransforms.parallaxToCM.label").c_str(),
                    &m_params.shaderTransforms.isParallaxToCMEnabled);

    ImGui::SeparatorText(tr("launcher.postPatchers.title").c_str());
    ImGui::Checkbox(tr("launcher.postPatchers.disablePrePatchedMaterials.label").c_str(),
                    &m_params.postPatcher.disablePrePatchedMaterials);
    ImGui::Checkbox(tr("launcher.postPatchers.fixSSS.label").c_str(), &m_params.postPatcher.isFixSSSEnabled);
    ImGui::Checkbox(tr("launcher.postPatchers.hairFlowMap.label").c_str(), &m_params.postPatcher.isHairFlowMapEnabled);
}

void Menu::drawRecordTypes()
{
    ImGui::TextWrapped("%s", tr("dialogs.recTypeSelector.description").c_str());

    const auto names = PGPlugin::availableRecTypeStrs();
    if (ImGui::BeginTable("recordtypes", 5)) {
        for (const auto& name : names) {
            const auto type = PGPlugin::recTypeFromString(name);
            ImGui::TableNextColumn();
            bool isAllowed = m_params.processing.allowedModelRecordTypes.contains(type);
            if (ImGui::Checkbox(name.c_str(), &isAllowed)) {
                if (isAllowed)
                    m_params.processing.allowedModelRecordTypes.insert(type);
                else
                    m_params.processing.allowedModelRecordTypes.erase(type);
            }
        }
        ImGui::EndTable();
    }
}

float Menu::buttonWidth(const char* label)
{
    return ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
}

void Menu::drawStringList(const char* id,
                          std::vector<std::string>& items,
                          std::string& newItem)
{
    // The text fields leave the room of the wider button, so the two columns line up.
    const auto addLabel = tr("components.modifiableList.add");
    const auto removeLabel = tr("components.modifiableList.remove");
    const auto spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto buttonSize = ImVec2(std::max(buttonWidth(removeLabel.c_str()), buttonWidth(addLabel.c_str())), 0);
    ImGui::PushID(id);
    for (std::size_t i = 0; i < items.size();) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-(buttonSize.x + spacing));
        ImGui::InputText("##entry", &items.at(i));
        ImGui::SameLine();
        const bool shouldRemove = ImGui::Button(removeLabel.c_str(), buttonSize);
        ImGui::PopID();
        if (shouldRemove)
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
        else
            i++;
    }

    ImGui::SetNextItemWidth(-(buttonSize.x + spacing));
    ImGui::InputText("##new", &newItem);
    ImGui::SameLine();
    if (ImGui::Button(addLabel.c_str(), buttonSize) && !newItem.empty()) {
        items.push_back(newItem);
        newItem.clear();
    }
    ImGui::PopID();
}

void Menu::drawMeshRules()
{
    ImGui::SeparatorText(tr("launcher.processing.meshAllowlist").c_str());
    ImGui::TextWrapped("%s", tr("pglite.meshRules.allowlist").c_str());
    drawStringList("allow", m_allowList, m_newAllowEntry);

    ImGui::SeparatorText(tr("launcher.processing.meshBlocklist").c_str());
    ImGui::TextWrapped("%s", tr("pglite.meshRules.blocklist").c_str());
    drawStringList("block", m_blockList, m_newBlockEntry);
}

void Menu::drawTextureRules()
{
    ImGui::TextWrapped("%s", tr("pglite.textureRules.description").c_str());
    const auto typeNames = PGEnums::texTypesStr();
    std::vector<const char*> typeLabels;
    typeLabels.reserve(typeNames.size());
    float comboWidth = 0;
    for (const auto& name : typeNames) {
        typeLabels.push_back(name.c_str());
        comboWidth = std::max(comboWidth, ImGui::CalcTextSize(name.c_str()).x);
    }

    // The path field leaves the room of the type combo and of the wider button.
    const auto addLabel = tr("components.modifiableList.add");
    const auto removeLabel = tr("components.modifiableList.remove");
    const auto& style = ImGui::GetStyle();
    comboWidth += style.FramePadding.x * 2 + ImGui::GetFrameHeight();
    const auto buttonSize = ImVec2(std::max(buttonWidth(removeLabel.c_str()), buttonWidth(addLabel.c_str())), 0);
    const auto pathWidth = -(comboWidth + buttonSize.x + style.ItemSpacing.x * 2);

    for (std::size_t i = 0; i < m_textureRules.size();) {
        auto& rule = m_textureRules.at(i);
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(pathWidth);
        ImGui::InputText("##path", &rule.path);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(comboWidth);
        ImGui::Combo("##type", &rule.typeIndex, typeLabels.data(), static_cast<int>(typeLabels.size()));
        ImGui::SameLine();
        const bool shouldRemove = ImGui::Button(removeLabel.c_str(), buttonSize);
        ImGui::PopID();
        if (shouldRemove)
            m_textureRules.erase(m_textureRules.begin() + static_cast<std::ptrdiff_t>(i));
        else
            i++;
    }

    ImGui::SetNextItemWidth(pathWidth);
    ImGui::InputText("##newpath", &m_newTexturePath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboWidth);
    ImGui::Combo("##newtype", &m_newTextureType, typeLabels.data(), static_cast<int>(typeLabels.size()));
    ImGui::SameLine();
    if (ImGui::Button(addLabel.c_str(), buttonSize) && !m_newTexturePath.empty()) {
        m_textureRules.push_back({ .path = m_newTexturePath, .typeIndex = m_newTextureType });
        m_newTexturePath.clear();
    }
}

void Menu::drawConflictManager()
{
    // The choices of the offline launcher: the type names of PGModManager, "None" with its suffix.
    const auto types = PGModManager::modManagerTypes();
    std::vector<std::string> labels;
    std::vector<const char*> labelPointers;
    int current = 0;
    for (std::size_t i = 0; i < types.size(); i++) {
        auto label = PGModManager::strFromModManagerType(types.at(i));
        if (types.at(i) == PGModManager::ModManagerType::None)
            label += tr("launcher.modManager.noneSuffix");
        labels.push_back(std::move(label));
        if (types.at(i) == m_params.modManager.type)
            current = static_cast<int>(i);
    }
    for (const auto& label : labels)
        labelPointers.push_back(label.c_str());

    // The label at the left of the dropdown, as the rows below have it; the dropdown is as wide as its longest choice.
    float comboWidth = 0;
    for (const auto& label : labels)
        comboWidth = std::max(comboWidth, ImGui::CalcTextSize(label.c_str()).x);
    comboWidth += ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight();
    ImGui::TextUnformatted(tr("launcher.modManager.title").c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo("##modmanager", &current, labelPointers.data(), static_cast<int>(labelPointers.size())))
        m_params.modManager.type = types.at(static_cast<std::size_t>(current));

    const bool isMO2 = m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2;
    if (isMO2) {
        const auto browseLabel = tr("common.browse");
        ImGui::TextUnformatted(tr("launcher.mo2Options.instanceLocation.label").c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-(buttonWidth(browseLabel.c_str()) + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##instance", &m_mo2InstanceDir);
        ImGui::SameLine();
        if (ImGui::Button(browseLabel.c_str()))
            browseForFolder(tr("launcher.browse.mo2InstanceLocation").c_str());
        ImGui::Checkbox(tr("conflictManager.lockMO2Order.label").c_str(),
                        &m_params.modManager.shouldUseMO2LooseFileOrder);
    } else if (m_params.modManager.type == PGModManager::ModManagerType::Vortex) {
        ImGui::TextWrapped("%s", tr("pglite.conflictManager.vortex").c_str());
    }
    ImGui::Separator();

    if (!m_runtime->isReady()) {
        ImGui::TextUnformatted(tr("pglite.conflictManager.notReady").c_str());
        return;
    }

    // The list follows the settings above as they are edited.
    rebuildModList();
    if (!m_modListMessage.empty()) {
        ImGui::TextWrapped("%s", m_modListMessage.c_str());
        return;
    }

    ImGui::TextWrapped("%s", tr("pglite.conflictManager.help").c_str());
    const bool canReorder = !(isMO2 && m_params.modManager.shouldUseMO2LooseFileOrder);
    if (canReorder)
        ImGui::TextWrapped("%s", tr("pglite.conflictManager.dragHint").c_str());

    if (!ImGui::BeginTable("mods", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY))
        return;

    const auto meshesOnly = tr("pglite.conflictManager.meshesOnly");
    const auto newSuffix = tr("pglite.conflictManager.newSuffix");
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##enabled", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(tr("conflictManager.columns.mod").c_str(), ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn(tr("conflictManager.columns.shader").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn(tr("components.checkedDragList.ignoreMeshes").c_str(), ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();

    int moveFrom = -1;
    int moveTo = -1;
    for (std::size_t i = 0; i < m_mods.size(); i++) {
        auto& row = m_mods.at(i);
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Checkbox("##enabled", &row.isEnabled);

        ImGui::TableNextColumn();
        const auto label = row.isNew ? row.displayName + newSuffix : row.displayName;
        const auto* const shaders
            = row.shaders.empty() ? (row.hasMeshes ? meshesOnly.c_str() : "") : row.shaders.c_str();
        // The selectable is as tall as the checkbox next to it, and the drop target covers the whole row including
        // the cell padding, so the rows leave no gap a mod cannot be dropped on.
        ImGui::Selectable(label.c_str(),
                          false,
                          ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(0, ImGui::GetFrameHeight()));
        ImRect rowRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        const auto rowPadding = ImGui::GetStyle().CellPadding.y;
        rowRect.Min.y -= rowPadding;
        rowRect.Max.y += rowPadding;
        if (canReorder) {
            // The ghost that follows the cursor shows the row as it is in the list, a little translucent.
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.85F);
            if (ImGui::BeginDragDropSource()) {
                const int index = static_cast<int>(i);
                ImGui::SetDragDropPayload("PGLITE_MOD", &index, sizeof(index));
                ImGui::TextUnformatted(label.c_str());
                if (*shaders) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", shaders);
                }
                ImGui::EndDragDropSource();
            }
            ImGui::PopStyleVar();
            if (ImGui::BeginDragDropTargetCustom(rowRect, ImGui::GetID("drop"))) {
                if (const auto* const payload = ImGui::AcceptDragDropPayload("PGLITE_MOD")) {
                    moveFrom = *static_cast<const int*>(payload->Data);
                    moveTo = static_cast<int>(i);
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(shaders);

        ImGui::TableNextColumn();
        ImGui::Checkbox("##ignored", &row.areMeshesIgnored);
        ImGui::PopID();
    }
    ImGui::EndTable();

    if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
        auto moved = m_mods.at(static_cast<std::size_t>(moveFrom));
        m_mods.erase(m_mods.begin() + moveFrom);
        m_mods.insert(m_mods.begin() + moveTo, std::move(moved));
    }
}

void Menu::drawMessages()
{
    const auto messages = Logging::messages();
    ImGui::Text(tr("pglite.messages.count").c_str(), messages.size());
    ImGui::SameLine();
    if (ImGui::Button(tr("pglite.messages.clear").c_str()))
        Logging::clearMessages();

    // EndChild is called whatever BeginChild returned, as Dear ImGui requires.
    if (ImGui::BeginChild("messages", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (const auto& message : messages) {
            const auto color
                = message.level >= spdlog::level::err ? ImVec4(1, 0.45F, 0.45F, 1) : ImVec4(1, 0.85F, 0.4F, 1);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", message.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
}

void Menu::drawSettings()
{
    if (m_languages.empty()) {
        ImGui::TextWrapped("%s", tr("pglite.settings.noTranslations").c_str());
        return;
    }

    // The languages of the translations folder by their own names; a change takes effect at once and is kept.
    std::vector<const char*> names;
    names.reserve(m_languages.size());
    int current = 0;
    const auto currentCode = PGTranslation::currentLanguage();
    for (std::size_t i = 0; i < m_languages.size(); i++) {
        const auto& language = m_languages.at(i);
        names.push_back(language.displayName.empty() ? language.code.c_str() : language.displayName.c_str());
        if (language.code == currentCode)
            current = static_cast<int>(i);
    }

    if (ImGui::Combo(tr("settings.language.label").c_str(), &current, names.data(), static_cast<int>(names.size()))) {
        const auto& code = m_languages.at(static_cast<std::size_t>(current)).code;
        std::ignore = Settings::writeLanguage(code);
        PGTranslation::init(Settings::translationsFolder(), code);
        m_shouldRebuildFonts = true;
    }
}

void Menu::rebuildFonts()
{
    auto& io = ImGui::GetIO();
    io.Fonts->Clear();

    // The Latin and Cyrillic ranges plus every glyph of the loaded translation and of the names of the languages the
    // settings page lists; a CJK font is merged for those.
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    for (const auto& text : PGTranslation::allStrings())
        builder.AddText(text.c_str());
    for (const auto& language : PGTranslation::availableLanguages())
        builder.AddText(language.displayName.c_str());
    m_glyphRanges.clear();
    builder.BuildRanges(&m_glyphRanges);
    bool isCJKNeeded = false;
    for (int i = 0; i + 1 < m_glyphRanges.Size; i += 2)
        isCJKNeeded = isCJKNeeded || m_glyphRanges[i + 1] >= firstCJKCodePoint;

    const auto size = fontSize * m_uiScale;
    const auto font = fontPath(L"segoeui.ttf");
    if (!io.Fonts->AddFontFromFileTTF(font.c_str(), size, nullptr, m_glyphRanges.Data)) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = m_uiScale;
    } else if (isCJKNeeded) {
        const auto cjkFont = fontPath(L"msyh.ttc");
        ImFontConfig merge;
        merge.MergeMode = true;
        if (!io.Fonts->AddFontFromFileTTF(cjkFont.c_str(), size, &merge, m_glyphRanges.Data))
            Logger::warn("The CJK font {} is unavailable, the translation shows placeholders for its glyphs", cjkFont);
    }
    io.Fonts->Build();

    // The backend uploads the new atlas on its next frame.
    if (m_isInitialized)
        ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Menu::browseForFolder(const char* title)
{
    {
        const std::scoped_lock lock(m_pickerMutex);
        if (m_isPickerOpen)
            return;

        m_isPickerOpen = true;
    }

    // The dialog needs a single-threaded apartment with a message loop of its own, which Show() runs; the game window
    // is its owner, so it stays in front of the game and the game keeps drawing meanwhile.
    std::thread([this, owner = m_gameWindow, dialogTitle = StringUtil::utf8toUTF16(title)] {
        std::optional<std::wstring> picked;
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) {
            Microsoft::WRL::ComPtr<IFileDialog> dialog;
            if (SUCCEEDED(
                    CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
                FILEOPENDIALOGOPTIONS options = 0;
                std::ignore = dialog->GetOptions(&options);
                std::ignore = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
                std::ignore = dialog->SetTitle(dialogTitle.c_str());
                Microsoft::WRL::ComPtr<IShellItem> item;
                wchar_t* path = nullptr;
                if (SUCCEEDED(dialog->Show(owner)) && SUCCEEDED(dialog->GetResult(&item))
                    && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    picked = path;
                    CoTaskMemFree(path);
                }
            }
            CoUninitialize();
        } else {
            Logger::error("The folder dialog could not initialize COM");
        }

        const std::scoped_lock lock(m_pickerMutex);
        m_isPickerOpen = false;
        if (picked)
            m_pickedFolder = StringUtil::utf16toUTF8(*picked);
    }).detach();
}

void Menu::pollPickedFolder()
{
    const std::scoped_lock lock(m_pickerMutex);
    if (!m_pickedFolder)
        return;

    m_mo2InstanceDir = *m_pickedFolder;
    m_pickedFolder.reset();
}

const char* Menu::clipboardText([[maybe_unused]] ImGuiContext* context)
{
    s_clipboard.clear();
    if (!OpenClipboard(nullptr))
        return nullptr;

    if (auto* const handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* const text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            s_clipboard = StringUtil::utf16toUTF8(text);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return s_clipboard.c_str();
}

void Menu::setClipboardText([[maybe_unused]] ImGuiContext* context,
                            const char* text)
{
    if (!text || !OpenClipboard(nullptr))
        return;

    const auto wide = StringUtil::utf8toUTF16(text);
    const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
    if (auto* const handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (auto* const buffer = GlobalLock(handle)) {
            std::memcpy(buffer, wide.c_str(), bytes);
            GlobalUnlock(handle);
            EmptyClipboard();
            if (!SetClipboardData(CF_UNICODETEXT, handle))
                GlobalFree(handle);
        } else {
            GlobalFree(handle);
        }
    }
    CloseClipboard();
}

}
