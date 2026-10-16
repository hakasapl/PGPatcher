#pragma once

#include "PGPlugin.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PGLite {

/**
 * @brief One model of a record: the component that holds the path and the sub model name the offline program gives it
 * ("MODL", "MALE", "FEMALE", "1STMALE", "1STFEMALE", "DMDL<stage>").
 */
struct ModelElement {
    /// @brief The component that holds the model path.
    RE::TESModel* model = nullptr;
    /// @brief The same component when it carries alternate textures, nullptr for a plain model.
    RE::TESModelTextureSwap* swap = nullptr;
    std::string subModel;
};

/**
 * @brief One use of a mesh by a record model, with everything the patchers and the permutation plan need.
 */
struct RecordUse {
    /// @brief Origin file, local form ID and sub model: what Mutagen's FormKey gives the offline program.
    PGMeshPermutationTracker::FormKey formKey;
    PGPlugin::MeshUseAttributes attributes;
    RE::TESForm* form = nullptr;
    ModelElement element;
};

/**
 * @brief The plugin uses of every mesh, read from the loaded game (DESIGN.md section 4.6): the runtime counterpart of
 * the offline Mutagen pass. The map is keyed by the lower case model path with the meshes\ prefix, normalized the way
 * the engine resolves model paths. Weighted armor addon models are listed under both their _0 and _1 paths.
 */
class Uses {
public:
    using UsesByPath = std::unordered_map<std::filesystem::path, std::vector<RecordUse>>;

    /**
     * @brief Reads every model record of the 36 record types from the data handler. At data load, before the game
     * runs.
     */
    void build();

    /**
     * @brief The uses by mesh path, each list in the order the offline program patches it.
     */
    [[nodiscard]] const UsesByPath& byPath() const;

    /**
     * @brief Normalizes a model string the way the engine resolves it: forward slashes become backslashes, the string
     * is lower cased and gets the meshes\ prefix unless it has one. Empty for an empty string.
     */
    [[nodiscard]] static std::filesystem::path normalizedModelPath(const char* model);

    /**
     * @brief The file name of the plugin that defines a form, empty for a form without a file.
     */
    [[nodiscard]] static std::wstring originFileName(const RE::TESForm& form);

    /**
     * @brief The eight paths of a texture set in NIF slot order, lower case and with the textures\ prefix, as the
     * offline program reads them. Eight empty strings for a null set.
     */
    [[nodiscard]] static PGTypes::TextureSet textureSet(RE::BGSTextureSet* set);

    /**
     * @brief The models of a form, in the order the offline program enumerates them.
     */
    [[nodiscard]] static std::vector<ModelElement> modelElements(RE::TESForm& form);

    /**
     * @brief The other file of a weighted pair (_0.nif for _1.nif and the reverse), empty for a path that ends in
     * neither.
     */
    [[nodiscard]] static std::filesystem::path weightCounterpart(const std::filesystem::path& path);

    /**
     * @brief The form types of the 36 record types with models, in the order build() reads them.
     */
    [[nodiscard]] static std::vector<RE::FormType> modelFormTypes();

private:
    UsesByPath m_byPath;
};

}
