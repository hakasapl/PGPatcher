#pragma once

#include "Uses.hpp"
#include "pgutil/PGTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

/**
 * @brief The record fields the permutation system edits, as they were before the first plan (DESIGN.md section 4.8),
 * and the model strings PGLite wrote since.
 *
 * Apply restores them before it plans again, which is what keeps the plan idempotent: indices are corrected once per
 * plan and contexts are built from unmodified data. Every model string PGLite writes goes through setModel(), which
 * remembers the value, so that a value another plugin wrote after PGLite planned (a data-load handler dispatched after
 * PGLite's, a script) is never overwritten and never restored over: PGLite only ever replaces what it wrote itself.
 *
 * Not thread safe: the writers are the data-load work, Apply (while the main thread waits) and the main-thread tasks
 * that retarget merged members, which never overlap.
 */
class RecordSnapshot {
public:
    /// @brief Captures every model element of the uses, each once. At data load.
    void capture(const Uses& uses);

    /**
     * @brief Points a captured record model at a mesh path, unless another plugin changed the model string since
     * PGLite last wrote it. A model that was not captured is written as is.
     *
     * @return false if the record keeps another plugin's value.
     */
    bool setModel(RE::TESModel& model,
                  const std::filesystem::path& meshPath);

    /**
     * @brief Writes the captured fields back: every model string that still holds what PGLite wrote, and every
     * alternate texture entry. Under the exclusive pipeline lock of Apply, while the main thread waits.
     */
    void restore();

    [[nodiscard]] std::size_t size() const { return m_elements.size(); }

private:
    struct AlternateTexture {
        RE::BGSTextureSet* textureSet = nullptr;
        std::uint32_t index3D = 0;
    };

    struct ElementState {
        RE::TESModel* model = nullptr;
        RE::TESModelTextureSwap* swap = nullptr;
        /// @brief The model string before the first plan.
        RE::BSFixedString modelPath;
        /// @brief The model string PGLite wrote last (the original until a retarget), as the engine stored it.
        RE::BSFixedString writtenPath;
        std::vector<AlternateTexture> alternateTextures;
    };

    std::vector<ElementState> m_elements;
    /// @brief The element of every captured model.
    std::unordered_map<const RE::TESModel*, std::size_t> m_captured;
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
