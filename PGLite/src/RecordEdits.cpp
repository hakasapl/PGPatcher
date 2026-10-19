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

/// @brief The engine's per-thread "form read from a plugin file" flag, byte 0x5DB of its thread local block, as an
/// index into RE::TLSData::unk5D0. TESForm's constructor gives a form the next dynamic form ID and puts it into the
/// global form map only while the flag is clear; the plugin loader sets it around the construction of every form it
/// reads, whose ID comes from the file. The same offset in SE 1.5.97, AE 1.6.1170 and 1.7.99 and VR 1.4.15.
constexpr std::size_t formFromFileFlagIndex = 0x5DB - offsetof(RE::TLSData, unk5D0);
static_assert(formFromFileFlagIndex < sizeof(RE::TLSData::unk5D0));

/// @brief Sets the form-from-file flag of the calling thread while it lives, and gives the flag its value back.
class FormFromFileScope {
public:
    FormFromFileScope()
        : m_flag(&RE::GetStaticTLSData()->unk5D0[formFromFileFlagIndex])
        , m_previous(*m_flag)
    {
        *m_flag = 1;
    }

    ~FormFromFileScope() { *m_flag = m_previous; }

    FormFromFileScope(const FormFromFileScope&) = delete;
    FormFromFileScope& operator=(const FormFromFileScope&) = delete;
    FormFromFileScope(FormFromFileScope&&) = delete;
    FormFromFileScope& operator=(FormFromFileScope&&) = delete;

private:
    std::uint8_t* m_flag;
    std::uint8_t m_previous;
};

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
            state.writtenAlternateTextures = state.alternateTextures;

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

bool RecordSnapshot::isModelOwned(const RE::TESModel& model) const
{
    const auto it = m_captured.find(&model);
    return it == m_captured.end() || model.model == m_elements.at(it->second).writtenPath;
}

bool RecordSnapshot::setAlternateTexture(RE::TESModel& model,
                                         RE::TESModelTextureSwap& swap,
                                         std::uint32_t entryIndex,
                                         std::uint32_t index3D,
                                         RE::BGSTextureSet* textureSet)
{
    auto& entry = swap.alternateTextures[entryIndex];
    const auto it = m_captured.find(&model);
    auto* const state = it == m_captured.end() ? nullptr : &m_elements.at(it->second);
    auto* const written = state && entryIndex < state->writtenAlternateTextures.size()
        ? &state->writtenAlternateTextures[entryIndex]
        : nullptr;
    if (written && (entry.textureSet != written->textureSet || entry.index3D != written->index3D)) {
        // Another plugin set the entry after PGLite did: its value stays.
        return false;
    }

    entry.index3D = index3D;
    entry.textureSet = textureSet;
    if (written)
        *written = { .textureSet = textureSet, .index3D = index3D };

    return true;
}

void RecordSnapshot::restore()
{
    std::size_t kept = 0;
    std::size_t keptEntries = 0;
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
            auto& written = state.writtenAlternateTextures[i];
            if (entry.textureSet != written.textureSet || entry.index3D != written.index3D) {
                keptEntries++;
                continue;
            }

            entry.textureSet = state.alternateTextures[i].textureSet;
            entry.index3D = state.alternateTextures[i].index3D;
            written = state.alternateTextures[i];
        }
    }

    if (kept || keptEntries) {
        Logger::info("{} record models and {} alternate texture entries keep the value another plugin set after "
                     "PGLite planned",
                     kept,
                     keptEntries);
    }
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

    // The set is never registered: no form ID and no entry in the global form map, so a save never stores it and a
    // load never purges it; the swap needs the pointer only. TESForm's constructor, which the factory runs, skips both
    // while the thread's form-from-file flag is set, so nothing takes the form map's lock on the thread that opens the
    // mesh (a loader thread, usually).
    auto* const factory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::BGSTextureSet>();
    RE::BGSTextureSet* set = nullptr;
    if (factory) {
        const FormFromFileScope formFromFile;
        set = factory->Create();
    }

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

    // A constructor that ignored the flag registered the set: it leaves the form map again.
    if (const auto formID = set->GetFormID()) {
        if (!m_didReportFormID) {
            Logger::warn("Texture set records get form IDs on this runtime (first {:08X}), PGLite takes them out of "
                         "the global form map again",
                         formID);
            m_didReportFormID = true;
        }

        const auto [forms, formsLock] = RE::TESForm::GetAllForms();
        const RE::BSWriteLockGuard guard { formsLock.get() };
        if (forms)
            forms->erase(formID);
        set->formID = 0;
    }

    m_sets.emplace(std::move(key), set);
    Logger::debug(L"Created texture set {} for {}", m_sets.size(), textures.front());
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
