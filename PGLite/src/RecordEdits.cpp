#include "PCH.h"

#include "RecordEdits.hpp"

#include "Uses.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace PGLite {

namespace {

/// @brief The record index (TX00 to TX07) of every NIF texture slot, see Uses::textureSet().
constexpr std::array<RE::BSTextureSet::Texture, 8> recordTextureOfSlot {
    RE::BSTextureSet::Texture::kDiffuse,
    RE::BSTextureSet::Texture::kNormal,
    RE::BSTextureSet::Texture::kGlowMap,
    RE::BSTextureSet::Texture::kHeight,
    RE::BSTextureSet::Texture::kEnvironment,
    RE::BSTextureSet::Texture::kEnvironmentMask,
    RE::BSTextureSet::Texture::kMultilayer,
    RE::BSTextureSet::Texture::kBacklightMask,
};

constexpr std::wstring_view meshesPrefix = L"meshes\\";
constexpr std::wstring_view texturesPrefix = L"textures\\";

}

void RecordSnapshot::capture(const Uses& uses)
{
    for (const auto& [path, recordUses] : uses.byPath()) {
        for (const auto& use : recordUses) {
            auto* const model = use.element.model;
            if (!model || !m_captured.insert(model).second)
                continue;

            ElementState state;
            state.model = model;
            state.swap = use.element.swap;
            state.modelPath = model->model;
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

void RecordSnapshot::restore() const
{
    for (const auto& state : m_elements) {
        state.model->model = state.modelPath;
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
}

RE::BGSTextureSet* TextureSetFactory::textureSet(const PGTypes::TextureSet& textures)
{
    // The key is the eight lower case paths; a set that differs in case only is the same set to the engine.
    std::wstring key;
    for (std::size_t slot = 0; slot < recordTextureOfSlot.size(); slot++) {
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

    for (std::size_t slot = 0; slot < recordTextureOfSlot.size(); slot++) {
        const auto& texture = textures.at(slot);
        std::wstring recordPath = texture;
        if (recordPath.starts_with(texturesPrefix))
            recordPath.erase(0, texturesPrefix.size());

        const auto path = StringUtil::utf16toWindows1252(recordPath);
        set->SetTexturePath(recordTextureOfSlot.at(slot), path.c_str());
    }

    if (m_sets.empty())
        Logger::debug("Created the first texture set record (form ID {:08X})", set->GetFormID());

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
