#include "PCH.h"

#include "ConsoleCommands.hpp"

#include "PGTranslation.hpp"
#include "Runtime.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace PGLite {

namespace {

/// @brief One command and the entries of the game's console table it can take over, in order of preference.
struct Command {
    const char* name = nullptr;
    std::array<std::string_view, 2> replacedCommands;
    const char* helpKey = nullptr;
    RE::SCRIPT_FUNCTION::Execute_t* execute = nullptr;
};

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
void run(RE::TESObjectREFR* reference,
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

// The parameters are the console's Execute_t signature, so they cannot take the const their use allows.
// NOLINTBEGIN(misc-const-correctness)
bool executeBlock([[maybe_unused]] const RE::SCRIPT_PARAMETER* paramInfo,
                  [[maybe_unused]] RE::SCRIPT_FUNCTION::ScriptData* scriptData,
                  RE::TESObjectREFR* thisObject,
                  [[maybe_unused]] RE::TESObjectREFR* containingObject,
                  [[maybe_unused]] RE::Script* script,
                  [[maybe_unused]] RE::ScriptLocals* locals,
                  [[maybe_unused]] double& result,
                  [[maybe_unused]] std::uint32_t& opcodeOffset)
{
    run(thisObject, true);
    return true;
}

bool executeAllow([[maybe_unused]] const RE::SCRIPT_PARAMETER* paramInfo,
                  [[maybe_unused]] RE::SCRIPT_FUNCTION::ScriptData* scriptData,
                  RE::TESObjectREFR* thisObject,
                  [[maybe_unused]] RE::TESObjectREFR* containingObject,
                  [[maybe_unused]] RE::Script* script,
                  [[maybe_unused]] RE::ScriptLocals* locals,
                  [[maybe_unused]] double& result,
                  [[maybe_unused]] std::uint32_t& opcodeOffset)
{
    run(thisObject, false);
    return true;
}
// NOLINTEND(misc-const-correctness)

/// @brief The commands, each with debug commands of the PlayStation 3 build to take over.
constexpr std::array<Command, 2> commands = {
    Command {
        .name = "pgblock",
        .replacedCommands = { "ToggleSPURenderBatch", "ToggleSPUCulling" },
        .helpKey = "pglite.console.blockHelp",
        .execute = &executeBlock,
    },
    Command {
        .name = "pgallow",
        .replacedCommands = { "ToggleSPUTransformUpdate", "ToggleSPUMovement" },
        .helpKey = "pglite.console.allowHelp",
        .execute = &executeAllow,
    },
};

}

void ConsoleCommands::install()
{
    // The console reads the help texts for as long as the game runs.
    static std::array<std::string, commands.size()> helpTexts;

    for (std::size_t i = 0; i < commands.size(); i++) {
        const auto& command = commands.at(i);
        RE::SCRIPT_FUNCTION* function = nullptr;
        for (const auto replaced : command.replacedCommands) {
            function = RE::SCRIPT_FUNCTION::LocateConsoleCommand(replaced);
            if (function)
                break;
        }

        if (!function) {
            Logger::warn("The console command {} is not installed: other plugins took over the console entries it "
                         "replaces",
                         command.name);
            continue;
        }

        Logger::debug("The console command {} replaces {}", command.name, function->functionName);
        helpTexts.at(i) = PGTranslation::tr(command.helpKey);
        function->functionName = command.name;
        function->shortName = command.name;
        function->helpString = helpTexts.at(i).c_str();
        // The command reads the selected reference itself, so that it can say so when none is selected.
        function->referenceFunction = false;
        function->SetParameters();
        function->executeFunction = command.execute;
        function->conditionFunction = nullptr;
    }
}

}
