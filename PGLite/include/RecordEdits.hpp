#pragma once

#include "Uses.hpp"
#include "pgutil/PGTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace PGLite {

/**
 * @brief The record fields the permutation system edits, as they were before the first plan (DESIGN.md section 4.8).
 *
 * Apply restores them before it plans again, which is what keeps the plan idempotent: indices are corrected once per
 * plan and contexts are built from unmodified data.
 */
class RecordSnapshot {
public:
    /// @brief Captures every model element of the uses, each once. Main thread.
    void capture(const Uses& uses);

    /// @brief Writes the captured fields back. Main thread, nothing may read the records concurrently.
    void restore() const;

    [[nodiscard]] std::size_t size() const { return m_elements.size(); }

private:
    struct AlternateTexture {
        RE::BGSTextureSet* textureSet = nullptr;
        std::uint32_t index3D = 0;
    };

    struct ElementState {
        RE::TESModel* model = nullptr;
        RE::TESModelTextureSwap* swap = nullptr;
        RE::BSFixedString modelPath;
        std::vector<AlternateTexture> alternateTextures;
    };

    std::vector<ElementState> m_elements;
    std::unordered_set<const RE::TESModel*> m_captured;
};

/**
 * @brief The texture set objects the patched alternate textures point at (DESIGN.md section 4.4): created through the
 * form factory and left unregistered, carrying the eight paths only, one object per distinct set of paths.
 */
class TextureSetFactory {
public:
    /**
     * @brief Returns the texture set for eight paths, creating it on first use. Thread safe.
     *
     * @param textures Lower case paths with the textures\ prefix in NIF slot order; the ninth slot is ignored.
     * @return The set, or nullptr if the engine's form factory is unavailable (logged once).
     */
    [[nodiscard]] RE::BGSTextureSet* textureSet(const PGTypes::TextureSet& textures);

    [[nodiscard]] std::size_t size() const;

private:
    std::unordered_map<std::wstring, RE::BGSTextureSet*> m_sets;
    mutable std::mutex m_mutex;
    bool m_didReportFailure = false;
};

/**
 * @brief Points a record model at a mesh path: the string is written the way plugins store it, without the meshes\
 * prefix.
 */
void setModelPath(RE::TESModel& model,
                  const std::filesystem::path& meshPath);

}
