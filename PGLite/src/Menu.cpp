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
#include <d3d11.h>
#include <dxgi.h>
#include <exception>
#include <filesystem>
#include <format>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

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

/// @brief The game controls the menu takes over while it is open.
constexpr auto frozenControls = static_cast<RE::ControlMap::UEFlag>(
    static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kMovement) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kLooking)
    | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kActivate) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kMenu)
    | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kConsole) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kPOVSwitch)
    | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kFighting) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kSneaking)
    | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kMainFour) | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kWheelZoom)
    | static_cast<std::uint32_t>(RE::ControlMap::UEFlag::kJumping));

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

/// @brief The entries of a list that are not hidden defaults.
std::vector<std::wstring> withoutDefaults(const std::vector<std::wstring>& list,
                                          const std::vector<std::wstring>& defaults)
{
    std::vector<std::wstring> result;
    for (const auto& entry : list)
        if (std::ranges::find(defaults, entry) == defaults.end())
            result.push_back(entry);
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

    if (!ImGui_ImplDX11_Init(m_device.Get(), m_context.Get())) {
        ImGui::DestroyContext();
        return false;
    }

    Logger::debug("Menu initialized Dear ImGui for a {} x {} back buffer", desc.BufferDesc.Width, desc.BufferDesc.Height);
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

    setGameControls(false);
}

void Menu::close()
{
    m_isOpen = false;
    setGameControls(true);
}

void Menu::setGameControls(bool isEnabled)
{
    if (auto* const controlMap = RE::ControlMap::GetSingleton())
        controlMap->ToggleControls(frozenControls, isEnabled, false);
}

void Menu::loadState()
{
    const auto defaults = PGParams::defaults();
    m_params = m_runtime->params();
    m_allowList = toUTF8(m_params.processing.allowList);
    m_blockList = toUTF8(withoutDefaults(m_params.processing.blockList, defaults.processing.blockList));

    m_textureRules.clear();
    const auto typeNames = PGEnums::texTypesStr();
    for (const auto& [path, type] : m_params.processing.textureMaps) {
        const auto name = PGEnums::strFromTexType(type);
        const auto it = std::ranges::find(typeNames, name);
        m_textureRules.push_back({ .path = StringUtil::utf16toUTF8(path),
                                   .typeIndex = it == typeNames.end() ? 0 : static_cast<int>(it - typeNames.begin()) });
    }

    m_mo2InstanceDir = StringUtil::utf16toUTF8(m_params.modManager.mo2InstanceDir.wstring());
    m_detectedMO2Instance = StringUtil::utf16toUTF8(Environment::detectMO2Instance().wstring());

    m_mods.clear();
    m_canReorderMods = !(m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2
                         && m_params.modManager.shouldUseMO2LooseFileOrder);
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
    const auto defaults = PGParams::defaults();

    // The lists the file holds: the user's entries; the defaults are added back when the file is read.
    PGParams stored = m_params;
    stored.processing.allowList = toUTF16(m_allowList);
    stored.processing.blockList = toUTF16(m_blockList);
    stored.processing.vanillaBSAList
        = withoutDefaults(m_params.processing.vanillaBSAList, defaults.processing.vanillaBSAList);
    stored.processing.textureMaps.clear();
    const auto typeNames = PGEnums::texTypesStr();
    for (const auto& rule : m_textureRules) {
        if (rule.path.empty() || rule.typeIndex < 0 || rule.typeIndex >= static_cast<int>(typeNames.size()))
            continue;

        stored.processing.textureMaps.emplace_back(StringUtil::utf8toUTF16(rule.path),
                                                   PGEnums::texTypeFromStr(typeNames.at(rule.typeIndex)));
    }
    stored.modManager.mo2InstanceDir = StringUtil::utf8toUTF16(m_mo2InstanceDir);

    if (!Settings::writeParams(stored)) {
        m_applyMessage = "The settings could not be written, see the log.";
        return;
    }

    // The runtime takes the full lists.
    PGParams runtimeParams = stored;
    runtimeParams.processing.blockList = defaults.processing.blockList;
    runtimeParams.processing.blockList.insert(
        runtimeParams.processing.blockList.end(), stored.processing.blockList.begin(), stored.processing.blockList.end());
    runtimeParams.processing.vanillaBSAList = defaults.processing.vanillaBSAList;
    runtimeParams.processing.vanillaBSAList.insert(runtimeParams.processing.vanillaBSAList.end(),
                                                   stored.processing.vanillaBSAList.begin(),
                                                   stored.processing.vanillaBSAList.end());

    // The mod rules: the displayed mods as ordered and set here, every other mod as it was.
    if (auto* const environment = m_runtime->environment()) {
        if (auto* const modManager = environment->modManager()) {
            auto rules = modManager->json();
            int priority = static_cast<int>(std::ranges::count_if(m_mods, [](const ModRow& row) { return row.isEnabled; }));
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

    m_applyMessage = m_runtime->apply(runtimeParams)
        ? "Applied. Meshes loaded from now on use the new settings; resident models change when the game reloads them."
        : "Apply failed, see the log.";
    m_isStateLoaded = false;
}

void Menu::draw()
{
    if (!m_isStateLoaded)
        loadState();

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
        if (ImGui::BeginTabItem("Record Types")) {
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
        if (ImGui::BeginTabItem("Mod Manager")) {
            drawModManager();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Mods")) {
            drawMods();
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
    if (status.disableReason) {
        ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.3F, 1.0F), "Disabled because %s.", status.disableReason->c_str());
    } else if (!status.isActive) {
        ImGui::TextUnformatted("Waiting for the game data.");
    } else if (!status.isReady) {
        ImGui::TextUnformatted("Initializing: the texture maps and the mod list are being built.");
    } else {
        ImGui::TextColored(ImVec4(0.5F, 1.0F, 0.5F, 1.0F), "Active.");
    }

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
    if (ImGui::Button("Apply", ImVec2(120, 0)))
        apply();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Reload", ImVec2(120, 0)))
        m_isStateLoaded = false;
    if (!m_applyMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextWrapped("%s", m_applyMessage.c_str());
    }
}

void Menu::drawPatchers()
{
    ImGui::SeparatorText("Pre-patchers");
    ImGui::Checkbox("Fix Mesh Lighting", &m_params.prePatcher.isFixMeshLightingEnabled);

    ImGui::SeparatorText("Shader patchers");
    ImGui::Checkbox("Parallax", &m_params.shaderPatcher.isParallaxEnabled);
    ImGui::Checkbox("Complex Material", &m_params.shaderPatcher.isComplexMaterialEnabled);
    ImGui::Checkbox("True PBR", &m_params.shaderPatcher.isTruePBREnabled);

    ImGui::SeparatorText("Post-patchers");
    ImGui::Checkbox("Restore Default Shaders (disable pre-patched materials)", &m_params.postPatcher.disablePrePatchedMaterials);
    ImGui::Checkbox("Hair Flow Map", &m_params.postPatcher.isHairFlowMapEnabled);

    ImGui::SeparatorText("Not available at runtime");
    ImGui::TextWrapped("The Parallax to Complex Material transform and Fix SSS generate textures, which PGLite does not do.");
}

void Menu::drawRecordTypes()
{
    ImGui::TextWrapped("A mesh use of an unchecked record type keeps the original, unpatched mesh.");
    if (ImGui::Button("All"))
        m_params.processing.allowedModelRecordTypes = PGPlugin::defaultRecTypeSet();
    ImGui::SameLine();
    if (ImGui::Button("None"))
        m_params.processing.allowedModelRecordTypes.clear();

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

void Menu::drawStringList(const char* id,
                          std::vector<std::string>& items,
                          std::string& newItem,
                          const char* hint)
{
    ImGui::PushID(id);
    for (std::size_t i = 0; i < items.size();) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-80);
        ImGui::InputText("##entry", &items.at(i));
        ImGui::SameLine();
        const bool shouldRemove = ImGui::Button("Remove");
        ImGui::PopID();
        if (shouldRemove)
            items.erase(items.begin() + static_cast<std::ptrdiff_t>(i));
        else
            i++;
    }

    ImGui::SetNextItemWidth(-80);
    ImGui::InputTextWithHint("##new", hint, &newItem);
    ImGui::SameLine();
    if (ImGui::Button("Add") && !newItem.empty()) {
        items.push_back(newItem);
        newItem.clear();
    }
    ImGui::PopID();
}

void Menu::drawMeshRules()
{
    ImGui::TextWrapped("Globs of mesh paths (MS-DOS wildcards, * matches any characters including backslashes). "
                       "Meshes on the block list are never patched; when the allow list is not empty, only meshes on "
                       "it are. The default block list (cameras, LOD, DynDOLOD, markers) always applies.");
    ImGui::SeparatorText("Allow list");
    drawStringList("allow", m_allowList, m_newAllowEntry, "meshes\\architecture\\*");
    ImGui::SeparatorText("Block list");
    drawStringList("block", m_blockList, m_newBlockEntry, "*\\effects\\*");
}

void Menu::drawTextureRules()
{
    ImGui::TextWrapped("Textures whose type is given here instead of being read from their file name suffix.");
    const auto typeNames = PGEnums::texTypesStr();
    std::vector<const char*> typeLabels;
    typeLabels.reserve(typeNames.size());
    for (const auto& name : typeNames)
        typeLabels.push_back(name.c_str());

    for (std::size_t i = 0; i < m_textureRules.size();) {
        auto& rule = m_textureRules.at(i);
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(-320);
        ImGui::InputText("##path", &rule.path);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220);
        ImGui::Combo("##type", &rule.typeIndex, typeLabels.data(), static_cast<int>(typeLabels.size()));
        ImGui::SameLine();
        const bool shouldRemove = ImGui::Button("Remove");
        ImGui::PopID();
        if (shouldRemove)
            m_textureRules.erase(m_textureRules.begin() + static_cast<std::ptrdiff_t>(i));
        else
            i++;
    }

    ImGui::SetNextItemWidth(-320);
    ImGui::InputTextWithHint("##newpath", "textures\\landscape\\dirt01_m.dds", &m_newTexturePath);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::Combo("##newtype", &m_newTextureType, typeLabels.data(), static_cast<int>(typeLabels.size()));
    ImGui::SameLine();
    if (ImGui::Button("Add") && !m_newTexturePath.empty()) {
        m_textureRules.push_back({ .path = m_newTexturePath, .typeIndex = m_newTextureType });
        m_newTexturePath.clear();
    }
}

void Menu::drawModManager()
{
    static const char* const typeLabels[] = { "None", "Vortex", "Mod Organizer 2" };
    int type = static_cast<int>(m_params.modManager.type);
    if (ImGui::Combo("Mod manager", &type, typeLabels, 3))
        m_params.modManager.type = static_cast<PGModManager::ModManagerType>(type);

    if (m_params.modManager.type == PGModManager::ModManagerType::ModOrganizer2) {
        ImGui::InputText("Instance folder", &m_mo2InstanceDir);
        if (m_detectedMO2Instance.empty()) {
            ImGui::TextWrapped("No running Mod Organizer 2 instance was detected. Start the game through Mod Organizer "
                               "2, or type the folder that holds its ModOrganizer.ini.");
        } else {
            ImGui::Text("Detected instance: %s", m_detectedMO2Instance.c_str());
            if (ImGui::Button("Use the detected instance"))
                m_mo2InstanceDir = m_detectedMO2Instance;
        }
        ImGui::Checkbox("Order mods like Mod Organizer 2 orders loose files", &m_params.modManager.shouldUseMO2LooseFileOrder);
    } else if (m_params.modManager.type == PGModManager::ModManagerType::Vortex) {
        ImGui::TextWrapped("The Vortex deployment manifest of the data folder is read.");
    } else {
        ImGui::TextWrapped("Without a mod manager, conflicting textures are resolved by shader quality and path only.");
    }
}

void Menu::drawMods()
{
    if (!m_runtime->isReady()) {
        ImGui::TextUnformatted("The mod list is available once the initialization finished.");
        return;
    }

    ImGui::TextWrapped("Mods with shaders or meshes, the highest priority first. A disabled mod's textures are not "
                       "used; a mod with its meshes ignored keeps its meshes unpatched.");
    if (m_canReorderMods)
        ImGui::TextWrapped("Drag a mod onto another one to move it there.");
    else
        ImGui::TextWrapped("The order follows Mod Organizer 2 (Mod Manager page).");

    if (!ImGui::BeginTable("mods", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY))
        return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Mod", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Shaders", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Meshes ignored", ImGuiTableColumnFlags_WidthFixed);
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
        ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap);
        if (m_canReorderMods) {
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
        const auto color = message.level >= spdlog::level::err ? ImVec4(1.0F, 0.45F, 0.45F, 1.0F)
                                                               : ImVec4(1.0F, 0.85F, 0.4F, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", message.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

}
