#include "PCH.h"

#include "ConsoleCommands.hpp"

#include "PGTranslation.hpp"
#include "Runtime.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

namespace PGLite {

namespace {

/// @brief The callback of the console menu's Scaleform delegate that the menu calls with the line typed.
constexpr std::string_view executeCommandCallback = "ExecuteCommand";

/// @brief One command: the first word of a console line, and what it runs with the selected reference.
struct Command {
    std::string_view name;
    const char* helpKey = nullptr;
    void (*run)(RE::TESObjectREFR* reference) = nullptr;
};

// The console menu lives for the session (kAlwaysOpen), and its delegate with it, so the callback is wrapped once per
// delegate: wrapping again over a plugin that wrapped it after PGLite would make the two call each other.
// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
RE::FxDelegateHandler::CallbackFn* s_original = nullptr;
/// @brief The game's own callback when PGLite found it in the slot, for a line that comes back to PGLite.
RE::FxDelegateHandler::CallbackFn* s_gameCallback = nullptr;
const RE::FxDelegate* s_wrappedDelegate = nullptr;
thread_local bool s_isInCallback = false;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

void print(const std::string& text)
{
    if (auto* const console = RE::ConsoleLog::GetSingleton())
        console->Print("%s", text.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
}

/**
 * @brief The model the engine loads for a reference, picked as TESObjectREFR's model lookup picks it (1.7.99
 * sub_1402EEBE0): the model swap of the reference, the model of its base object, or the world model of an armor for the
 * sex of the NPC that owns the reference.
 *
 * @return The model, or nullptr for an actor and for an object without a model.
 */
RE::TESModel* referenceModel(RE::TESObjectREFR& reference)
{
    if (const auto* const swap = reference.extraList.GetByType<RE::ExtraModelSwap>(); swap && swap->modelSwap)
        return swap->modelSwap;

    auto* const base = reference.GetBaseObject();
    if (!base || base->Is(RE::FormType::NPC))
        return nullptr;

    if (auto* const model = base->As<RE::TESModel>())
        return model;

    auto* const biped = base->As<RE::TESBipedModelForm>();
    if (!biped)
        return nullptr;

    // The male world model unless a female NPC owns the reference and the armor has a female one (1.7.99
    // sub_1401DF750).
    const auto* const owner = reference.GetOwner();
    const auto* const npc = owner ? owner->As<RE::TESNPC>() : nullptr;
    auto& femaleModel = biped->worldModels[RE::TESBipedModelForm::Sexes::kFemale];
    if (npc && npc->IsFemale() && !femaleModel.model.empty())
        return &femaleModel;

    return &biped->worldModels[RE::TESBipedModelForm::Sexes::kMale];
}

/// @brief Changes the block list for the mesh of the selected reference and says in the console what happened.
void setBlocked(RE::TESObjectREFR* reference,
                bool isBlocked)
{
    auto& runtime = Runtime::instance();
    const auto* const model = reference ? referenceModel(*reference) : nullptr;
    const auto meshPath = model ? runtime.originalMeshPath(*model) : std::filesystem::path();
    if (meshPath.empty()) {
        print(PGTranslation::tr("pglite.console.noMesh"));
        return;
    }

    const auto mesh = StringUtil::utf16toUTF8(meshPath.wstring());
    switch (runtime.setMeshBlocked(meshPath, isBlocked)) {
    case Runtime::BlockListChange::Added:
        Logger::info(L"Console: {} added to the mesh block list", meshPath.wstring());
        print(PGTranslation::trFormat("pglite.console.blocked", mesh));
        break;
    case Runtime::BlockListChange::Removed:
        Logger::info(L"Console: {} removed from the mesh block list", meshPath.wstring());
        print(PGTranslation::trFormat("pglite.console.allowed", mesh));
        break;
    case Runtime::BlockListChange::Unchanged:
        print(PGTranslation::trFormat(isBlocked ? "pglite.console.alreadyBlocked" : "pglite.console.notBlocked", mesh));
        break;
    case Runtime::BlockListChange::NotWritten:
        print(PGTranslation::tr("pglite.console.notWritten"));
        break;
    }
}

constexpr std::array<Command, 2> commands = {
    Command {
        .name = "pgblock",
        .helpKey = "pglite.console.blockHelp",
        .run = [](RE::TESObjectREFR* reference) { setBlocked(reference, true); },
    },
    Command {
        .name = "pgallow",
        .helpKey = "pglite.console.allowHelp",
        .run = [](RE::TESObjectREFR* reference) { setBlocked(reference, false); },
    },
};

bool isSameWord(std::string_view a,
                std::string_view b)
{
    return std::ranges::equal(a, b, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    });
}

/// @brief The words of a console line, split at white space.
std::vector<std::string> wordsOf(std::string_view line)
{
    std::vector<std::string> words;
    std::string word;
    for (const char ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!word.empty())
                words.push_back(std::exchange(word, { }));
        } else {
            word.push_back(ch);
        }
    }
    if (!word.empty())
        words.push_back(std::move(word));

    return words;
}

/// @brief The console menu's ExecuteCommand callback, wrapped: a line of a PGLite command runs on the main thread, any
/// other goes on to the callback that was in the slot.
void executeCommand(const RE::FxDelegateArgs& params)
{
    const char* const line = params.GetArgCount() && params[0].IsString() ? params[0].GetString() : nullptr;
    const auto words = line ? wordsOf(line) : std::vector<std::string>();
    const Command* command = nullptr;
    for (const auto& entry : commands)
        if (!words.empty() && isSameWord(words.front(), entry.name))
            command = &entry;
    if (command) {
        // A command takes no argument: one given gets the command's help. The game's callback reads the selected
        // reference here too.
        const bool isHelp = words.size() > 1;
        SKSE::GetTaskInterface()->AddTask([command, isHelp, selected = RE::Console::GetSelectedRef()] {
            if (isHelp)
                print(std::string(command->name) + ": " + PGTranslation::tr(command->helpKey));
            else
                command->run(selected.get());
        });
        return;
    }

    // A plugin that wrapped the callback again after PGLite and calls PGLite from there brings the line back here: the
    // game's own callback ends the loop when PGLite knows it, the line is dropped otherwise.
    if (s_isInCallback) {
        if (s_gameCallback) {
            s_gameCallback(params);
        } else {
            Logger::error("The console's {} callback loops through PGLite and another plugin, a console line is "
                          "dropped",
                          executeCommandCallback);
        }
        return;
    }

    if (s_original) {
        s_isInCallback = true;
        s_original(params);
        s_isInCallback = false;
    }
}

/// @brief Whether a callback is the game's own, in its executable, rather than another plugin's.
bool isInGame(RE::FxDelegateHandler::CallbackFn* callback)
{
    HMODULE module = nullptr;
    const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the module a function is in.
    return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(callback), &module) != 0
        && module == GetModuleHandleW(nullptr);
}

/// @brief Puts PGLite's callback in the console menu's slot, once per delegate.
void wrapCallback()
{
    auto* const ui = RE::UI::GetSingleton();
    const auto menu = ui ? ui->GetMenu(RE::Console::MENU_NAME) : nullptr;
    if (!menu || !menu->fxDelegate || menu->fxDelegate.get() == s_wrappedDelegate)
        return;

    for (auto& entry : menu->fxDelegate->callbacks) {
        if (std::string_view(entry.first.c_str()) != executeCommandCallback)
            continue;

        auto& callback = entry.second.callback;
        if (callback != &executeCommand) {
            s_original = callback;
            if (isInGame(callback))
                s_gameCallback = callback;
            callback = &executeCommand;
        }
        s_wrappedDelegate = menu->fxDelegate.get();
        Logger::debug("The console's {} callback goes through PGLite", executeCommandCallback);
        return;
    }

    Logger::warn("The console menu has no {} callback: the PGLite console commands are not available",
                 executeCommandCallback);
}

class ConsoleMenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                          RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*source*/) override
    {
        if (event && event->opening && event->menuName == RE::Console::MENU_NAME)
            wrapCallback();

        return RE::BSEventNotifyControl::kContinue;
    }
};

}

void ConsoleCommands::install()
{
    static ConsoleMenuSink sink;
    if (auto* const ui = RE::UI::GetSingleton())
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&sink);

    wrapCallback();
}

}
