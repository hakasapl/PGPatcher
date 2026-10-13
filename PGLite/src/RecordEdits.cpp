#include "PCH.h"

#include "RecordEdits.hpp"

#include "Uses.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace PGLite {

namespace {

/// @brief The slots of a texture set record (TX00 to TX07); the ninth NIF slot never reaches a record.
constexpr std::size_t recordSlotCount = 8;

constexpr std::wstring_view meshesPrefix = L"meshes\\";
constexpr std::wstring_view texturesPrefix = L"textures\\";

}

void RecordSnapshot::capture(const Uses& uses)
{
    for (const auto& [path, recordUses] : uses.byPath()) {
        for (const auto& use : recordUses) {
            auto* const model = use.element.model;
            if (!model || !m_captured.emplace(model, m_elements.size()).second)
                continue;

            ElementState state;
            state.model = model;
            state.swap = use.element.swap;
            state.modelPath = model->model;
            state.writtenPath = model->model;
            if (state.swap && state.swap->alternateTextures) {
                state.alternateTextures.reserve(state.swap->numAlternateTextures);
                for (std::uint32_t i = 0; i < state.swap->numAlternateTextures; i++) {
                    const auto& entry = state.swap->alternateTextures[i];
                    state.alternateTextures.push_back({ .textureSet = entry.textureSet, .index3D = entry.index3D });
                }
            }

            m_elements.push_back(std::move(state));
        }
    }
}

bool RecordSnapshot::setModel(RE::TESModel& model,
                              const std::filesystem::path& meshPath)
{
    const auto it = m_captured.find(&model);
    if (it == m_captured.end()) {
        setModelPath(model, meshPath);
        return true;
    }

    auto& state = m_elements.at(it->second);
    if (model.model != state.writtenPath) {
        // Another plugin set the string after PGLite did: its value stays, whatever PGLite planned for the record.
        Logger::debug("A record model keeps the value another plugin set: {}", model.model.c_str());
        return false;
    }

    setModelPath(model, meshPath);
    state.writtenPath = model.model;
    return true;
}

void RecordSnapshot::restore()
{
    std::size_t kept = 0;
    for (auto& state : m_elements) {
        if (state.model->model == state.writtenPath) {
            state.model->model = state.modelPath;
            state.writtenPath = state.modelPath;
        } else {
            kept++;
        }

        if (!state.swap || !state.swap->alternateTextures)
            continue;

        // The array itself is never resized, so the captured entries match the current ones.
        const auto count = std::min<std::size_t>(state.alternateTextures.size(), state.swap->numAlternateTextures);
        for (std::size_t i = 0; i < count; i++) {
            auto& entry = state.swap->alternateTextures[i];
            entry.textureSet = state.alternateTextures[i].textureSet;
            entry.index3D = state.alternateTextures[i].index3D;
        }
    }

    if (kept)
        Logger::info("{} record models keep the model string another plugin set after PGLite planned", kept);
}

RE::BGSTextureSet* TextureSetFactory::textureSet(const PGTypes::TextureSet& textures)
{
    // The key is the eight lower case paths; a set that differs in case only is the same set to the engine.
    std::wstring key;
    for (std::size_t slot = 0; slot < recordSlotCount; slot++) {
        key += textures.at(slot);
        key += L'|';
    }

    const std::scoped_lock lock(m_mutex);
    const auto it = m_sets.find(key);
    if (it != m_sets.end())
        return it->second;

    auto* const factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSTextureSet>();
    auto* const set = factory ? factory->Create() : nullptr;
    if (!set) {
        if (!m_didReportFailure) {
            Logger::error("Unable to create texture set records, patched alternate textures keep their original sets");
            m_didReportFailure = true;
        }

        return nullptr;
    }

    // The slots are written in NIF slot order: the engine's SetTexturePath maps a slot to its record field itself
    // (TX02 to TX05 are stored in another order than the NIF slots), as Uses::textureSet() reads them.
    for (std::size_t slot = 0; slot < recordSlotCount; slot++) {
        const auto& texture = textures.at(slot);
        std::wstring recordPath = texture;
        if (recordPath.starts_with(texturesPrefix))
            recordPath.erase(0, texturesPrefix.size());

        const auto path = StringUtil::utf16toWindows1252(recordPath);
        set->SetTexturePath(static_cast<RE::BSTextureSet::Texture>(slot), path.c_str());
    }

    // The constructor the factory runs gives the form the next dynamic form ID and puts it into the global form map
    // (TESForm's constructor, 1.7.99), which is where a save load would meet it. The set is unregistered again, as
    // DESIGN.md 4.4 requires: no form ID, no map entry, never saved, never purged. The swap needs the pointer only.
    const auto formID = set->GetFormID();
    if (formID) {
        const auto [forms, formsLock] = RE::TESForm::GetAllForms();
        const RE::BSWriteLockGuard guard { formsLock.get() };
        if (forms)
            forms->erase(formID);
    }
    set->formID = 0;

    if (m_sets.empty())
        Logger::debug("Created the first texture set record (form ID {:08X} before unregistering it)", formID);

    m_sets.emplace(std::move(key), set);
    return set;
}

std::size_t TextureSetFactory::size() const
{
    const std::scoped_lock lock(m_mutex);
    return m_sets.size();
}

void setModelPath(RE::TESModel& model,
                  const std::filesystem::path& meshPath)
{
    std::wstring recordPath = meshPath.wstring();
    if (recordPath.starts_with(meshesPrefix))
        recordPath.erase(0, meshesPrefix.size());

    const auto path = StringUtil::utf16toWindows1252(recordPath);
    model.SetModel(path.c_str());
}

}
