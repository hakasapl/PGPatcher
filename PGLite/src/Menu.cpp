#include "PCH.h"

#include "Menu.hpp"

#include "Environment.hpp"
#include "Logging.hpp"
#include "PGModManager.hpp"
#include "PGParams.hpp"
#include "PGPlugin.hpp"
#include "Runtime.hpp"
#include "Settings.hpp"
#include "pgutil/PGEnums.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_stdlib.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
constexpr const char* fontFile = "C:\\Windows\\Fonts\\segoeui.ttf";

// The labels of the offline launcher (PGPatcher/translations/en.json), so both programs read the same.
constexpr const char* addLabel = "Add";
constexpr const char* removeLabel = "Remove";
constexpr const char* browseLabel = "Browse";
constexpr const char* mo2InstanceDialogTitle = "Select MO2 Instance Location";

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
    , m_lastFrame(std::chrono::steady_clock::now())
{
}

Menu::~Menu()
{
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

    if (!io.Fonts->AddFontFromFileTTF(fontFile, fontSize * scale)) {
        io.Fonts->AddFontDefault();
        io.FontGlobalScale = scale;
    }
    ImGui::GetStyle().ScaleAllSizes(scale);
    ImGui::StyleColorsDark();

    // Ctrl+C and Ctrl+V in the text fields; the DX11 backend brings no clipboard of its own.
    auto& platformIO = ImGui::GetPlatformIO();
    platformIO.Platform_GetClipboardTextFn = &clipboardText;
    platformIO.Platform_SetClipboardTextFn = &setClipboardText;
    m_gameWindow = desc.OutputWindow;

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
    if (!m_isOpen && !m_didAutoOpen && m_runtime->ini().shouldOpenMenuAtStart && m_runtime->isReady()) {
        // A development aid (PGLite.ini [Debug] bOpenMenuAtStart): the menu without the hotkey.
        m_didAutoOpen = true;
        Logger::debug("Opening the menu at start (bOpenMenuAtStart)");
        open();
    }

    if (m_didFailInitialization || !swapChain)
        return;

    try {
        if (!m_isInitialized && m_isOpen && !initialize(swapChain)) {
            m_didFailInitialization = true;
            Logger::error("The menu could not initialize Dear ImGui on the game's device");
            m_isOpen = false;
            return;
        }

        // The hotkey and, while open, the mouse and keys queued by the input threads.
        drainInput();
        if (!m_isOpen)
            return;
        if (!m_isInitialized && !initialize(swapChain)) {
            m_didFailInitialization = true;
            Logger::error("The menu could not initialize Dear ImGui on the game's device");
            m_isOpen = false;
            return;
        }

        if (!m_backBufferView && !createBackBufferView(swapChain))
            return;

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
        draw();
        ImGui::Render();

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
    } catch (const std::exception& e) {
        Logger::error("The menu failed to draw and is closed: {}", e.what());
        close();
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
    if (button && isKeyboard && button->IsDown())
        Logger::trace("Key down: scan code {:#x}", button->GetIDCode());

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
        io.AddMouseWheelEvent(0, record.code == mouseWheelUp ? 1.0F : -1.0F);
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
    m_isOpen = true;
    m_isStateLoaded = false;
    m_applyMessage.clear();
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

    m_mods.clear();
    if (auto* const environment = m_runtime->isReady() ? m_runtime->environment() : nullptr) {
        if (auto* const modManager = environment->modManager()) {
            for (const auto& mod : modManager->modsByPriority()) {
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
    }

    m_isStateLoaded = true;
}

void Menu::apply()
{
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

    if (!Settings::writeParams(params)) {
        m_applyMessage = "The settings could not be written, see the log.";
        return;
    }

    // The mod rules: the displayed mods as ordered and set here, every other mod as it was.
    if (auto* const environment = m_runtime->environment()) {
        if (auto* const modManager = environment->modManager()) {
            auto rules = modManager->json();
            int priority
                = static_cast<int>(std::ranges::count_if(m_mods, [](const ModRow& row) { return row.isEnabled; }));
            for (const auto& row : m_mods) {
                auto& rule = rules[row.displayName];
                rule["priority"] = row.isEnabled ? priority-- : -1;
                rule["enabled"] = row.isEnabled;
                rule["meshesignored"] = row.areMeshesIgnored;
            }

            if (!Settings::writeModRules(rules)) {
                m_applyMessage = "The mod rules could not be written, see the log.";
                return;
            }
        }
    }

    m_applyMessage = m_runtime->apply(params)
        ? "Applied. Meshes loaded from now on use the new settings; resident models change when the game reloads them."
        : "Apply failed, see the log.";
    m_isStateLoaded = false;
}

void Menu::draw()
{
    if (!m_isStateLoaded)
        loadState();
    pollPickedFolder();

    const auto& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.6F, io.DisplaySize.y * 0.7F), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.2F, io.DisplaySize.y * 0.15F), ImGuiCond_FirstUseEver);

    bool isOpen = true;
    if (!ImGui::Begin("PGLite", &isOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("pages")) {
        if (ImGui::BeginTabItem("Status")) {
            drawStatus();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Patchers")) {
            drawPatchers();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mod Manager")) {
            drawModManager();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Conflict Manager")) {
            drawMods();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Allowed Record Types")) {
            drawRecordTypes();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mesh Rules")) {
            drawMeshRules();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Texture Rules")) {
            drawTextureRules();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Messages")) {
            drawMessages();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();

    if (!isOpen)
        close();
}

void Menu::drawStatus()
{
    const auto status = m_runtime->status();

    ImGui::Text("PGLite %s", PG_FULL_VERSION);
    if (status.disableReason)
        ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.3F, 1.0F), "Disabled because %s.", status.disableReason->c_str());
    else if (!status.isActive)
        ImGui::TextUnformatted("Waiting for the game data.");
    else if (!status.isReady)
        ImGui::TextUnformatted("Initializing: the texture maps and the mod list are being built.");
    else
        ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F), "Active.");

    ImGui::Separator();
    ImGui::Text("Record model uses: %zu of %zu meshes", status.uses, status.usedMeshes);
    ImGui::Text("Mesh families: %zu planned, %zu patched so far", status.families, status.patchedFamilies);
    ImGui::Text("Variant paths: %zu", status.variants);
    ImGui::Text("Registered streams: %zu", status.streams);
    ImGui::Text("Texture set records created: %zu", status.textureSets);
    if (!status.mo2Instance.empty())
        ImGui::Text("Mod Organizer 2 instance: %s", StringUtil::utf16toUTF8(status.mo2Instance.wstring()).c_str());

    ImGui::Separator();
    ImGui::TextWrapped("Apply saves every page to settings.json and modrules.json and plans the meshes again. Meshes "
                       "loaded from then on use the new settings; models the game already holds keep their current "
                       "state until it reloads them.");
    const bool canApply = status.isActive && status.isReady && !status.disableReason;
    ImGui::BeginDisabled(!canApply);
    if (ImGui::Button("Apply"))
        apply();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Discard Changes"))
        m_isStateLoaded = false;
    if (!m_applyMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_applyMessage.c_str());
    }
}

void Menu::drawPatchers()
{
    ImGui::SeparatorText("Pre-Patchers");
    ImGui::Checkbox("Fix Mesh Lighting (ENB Only)", &m_params.prePatcher.isFixMeshLightingEnabled);
    ImGui::SetItemTooltip("Fixes glowing meshes (For ENB users only!)");

    ImGui::SeparatorText("Shader Patchers");
    ImGui::Checkbox("Parallax", &m_params.shaderPatcher.isParallaxEnabled);
    ImGui::Checkbox("Complex Material", &m_params.shaderPatcher.isComplexMaterialEnabled);
    ImGui::Checkbox("TruePBR (CS Only)", &m_params.shaderPatcher.isTruePBREnabled);

    ImGui::SeparatorText("Post-Patchers");
    ImGui::Checkbox("Disable Pre-Patched Materials", &m_params.postPatcher.disablePrePatchedMaterials);
    ImGui::SetItemTooltip("Restores shaders to default if parallax or complex material textures are missing (highly "
                          "recommended, replaces auto parallax functionality)");
    ImGui::Checkbox("Add Hair Flow Map (CS Only)", &m_params.postPatcher.isHairFlowMapEnabled);
    ImGui::SetItemTooltip("Adds flow maps to texture sets for those that match the normal texture");

    ImGui::SeparatorText("Not available at runtime");
    ImGui::TextWrapped("Upgrade Parallax to Complex Material and Fix Vanilla Subsurface Scattering generate textures, "
                       "which PGLite does not do.");
}

void Menu::drawRecordTypes()
{
    ImGui::TextWrapped("Unchecking a record type will exclude it and its associated meshes from being patched. Only "
                       "record types with models are shown.");

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
    const auto spacing = ImGui::GetStyle().ItemSpacing.x;
    const auto buttonSize = ImVec2(std::max(buttonWidth(removeLabel), buttonWidth(addLabel)), 0);
    ImGui::PushID(id);
    for (std::size_t i = 0; i < items.size();) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-(buttonSize.x + spacing));
        ImGui::InputText("##entry", &items.at(i));
        ImGui::SameLine();
        const bool shouldRemove = ImGui::Button(removeLabel, buttonSize);
        ImGui::PopID();
        if (shouldRemove)
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
        else
            i++;
    }

    ImGui::SetNextItemWidth(-(buttonSize.x + spacing));
    ImGui::InputText("##new", &newItem);
    ImGui::SameLine();
    if (ImGui::Button(addLabel, buttonSize) && !newItem.empty()) {
        items.push_back(newItem);
        newItem.clear();
    }
    ImGui::PopID();
}

void Menu::drawMeshRules()
{
    ImGui::SeparatorText("Mesh Allowlist");
    ImGui::TextWrapped("If any rules exist here, only meshes matching them will be patched. Enter path to mesh like "
                       "\"meshes/armor/helmet.nif\" or use wildcards (* is the wildcard) to allowlist entire "
                       "folders/files.");
    drawStringList("allow", m_allowList, m_newAllowEntry);

    ImGui::SeparatorText("Mesh Blocklist");
    ImGui::TextWrapped("Any meshes matching rules here will not be patched. Enter path to mesh like "
                       "\"meshes/armor/helmet.nif\" or use wildcards (* is the wildcard) to blocklist entire "
                       "folders/files.");
    drawStringList("block", m_blockList, m_newBlockEntry);
}

void Menu::drawTextureRules()
{
    ImGui::TextWrapped("Use this to tell PGPatcher what type of texture something is if the auto detection is wrong "
                       "(very rare). Enter the full path to the texture like \"textures/armor/helmet.dds\" and select "
                       "the type of texture. Wildcards are NOT supported here. A texture can be ignored by setting it "
                       "to \"unknown\".");
    const auto typeNames = PGEnums::texTypesStr();
    std::vector<const char*> typeLabels;
    typeLabels.reserve(typeNames.size());
    float comboWidth = 0;
    for (const auto& name : typeNames) {
        typeLabels.push_back(name.c_str());
        comboWidth = std::max(comboWidth, ImGui::CalcTextSize(name.c_str()).x);
    }

    // The path field leaves the room of the type combo and of the wider button.
    const auto& style = ImGui::GetStyle();
    comboWidth += style.FramePadding.x * 2 + ImGui::GetFrameHeight();
    const auto buttonSize = ImVec2(std::max(buttonWidth(removeLabel), buttonWidth(addLabel)), 0);
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
        const bool shouldRemove = ImGui::Button(removeLabel, buttonSize);
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
    if (ImGui::Button(addLabel, buttonSize) && !m_newTexturePath.empty()) {
        m_textureRules.push_back({ .path = m_newTexturePath, .typeIndex = m_newTextureType });
        m_newTexturePath.clear();
    }
}

void Menu::drawModManager()
{
    // The choices of the offline launcher: the type names of PGModManager, "None" with its suffix.
    const auto types = PGModManager::modManagerTypes();
    std::vector<std::string> labels;
    std::vector<const char*> labelPointers;
    int current = 0;
    for (std::size_t i = 0; i < types.size(); i++) {
        auto label = PGModManager::strFromModManagerType(types.at(i));
        if (types.at(i) == PGModManager::ModManagerType::None)
            label += " (No Conflict Resolution)";
        labels.push_back(std::move(label));
        if (types.at(i) == m_params.modManager.type)
            current = static_cast<int>(i);
    }
    for (const auto& label : labels)
        labelPointers.push_back(label.c_str());

    if (ImGui::Combo("Mod Manager", &current, labelPointers.data(), static_cast<int>(labelPointers.size())))
        m_params.modManager.type = types.at(static_cast<std::size_t>(current));

    if (m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2) {
        ImGui::SeparatorText("MO2 Options");
        ImGui::TextUnformatted("Instance Location");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-(buttonWidth(browseLabel) + ImGui::GetStyle().ItemSpacing.x));
        ImGui::InputText("##instance", &m_mo2InstanceDir);
        ImGui::SetItemTooltip("Path to the MO2 instance folder (Folder Icon > Open Instance folder in MO2)");
        ImGui::SameLine();
        if (ImGui::Button(browseLabel))
            browseForFolder(mo2InstanceDialogTitle);
    } else if (m_params.modManager.type == PGModManager::ModManagerType::Vortex) {
        ImGui::TextWrapped("The Vortex deployment manifest of the data folder is read.");
    }
}

void Menu::drawMods()
{
    if (!m_runtime->isReady()) {
        ImGui::TextUnformatted("The mod list is available once the initialization finished.");
        return;
    }

    ImGui::TextWrapped("Please sort your mods to determine what mod PGPatcher uses to patch meshes where.");
    const bool isMO2 = m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2;
    if (isMO2) {
        ImGui::Checkbox("Lock to MO2 Loose File Order", &m_params.modManager.shouldUseMO2LooseFileOrder);
        ImGui::SetItemTooltip("Locks order to MO2. Enable/disable is still enabled. Keep in mind that PG conflicts are "
                              "not the same as loose file conflicts.");
    }
    const bool canReorder = !(isMO2 && m_params.modManager.shouldUseMO2LooseFileOrder);
    if (canReorder)
        ImGui::TextWrapped("Drag a mod onto another one to move it there.");

    if (!ImGui::BeginTable("mods", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##enabled", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Shader", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Ignore Meshes", ImGuiTableColumnFlags_WidthFixed);
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
        const auto label = row.isNew ? row.displayName + " (new)" : row.displayName;
        ImGui::Selectable(
            label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
        if (canReorder) {
            if (ImGui::BeginDragDropSource()) {
                const int index = static_cast<int>(i);
                ImGui::SetDragDropPayload("PGLITE_MOD", &index, sizeof(index));
                ImGui::TextUnformatted(row.displayName.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const auto* const payload = ImGui::AcceptDragDropPayload("PGLITE_MOD")) {
                    moveFrom = *static_cast<const int*>(payload->Data);
                    moveTo = static_cast<int>(i);
                }
                ImGui::EndDragDropTarget();
            }
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(row.shaders.empty() ? (row.hasMeshes ? "meshes only" : "") : row.shaders.c_str());

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
    ImGui::Text("%zu warnings and errors this session", messages.size());
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        Logging::clearMessages();

    if (!ImGui::BeginChild("messages", ImVec2(0, 0), ImGuiChildFlags_Borders))
        return;

    for (const auto& message : messages) {
        const auto color
            = message.level >= spdlog::level::err ? ImVec4(1.0F, 0.45F, 0.45F, 1.0F) : ImVec4(1.0F, 0.85F, 0.4F, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", message.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
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
