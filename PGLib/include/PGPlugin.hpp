#pragma once

#include "common/BethesdaGame.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/EnumStringHelper.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

/**
 * @brief Provides plugin (ESP/ESM) reading, object population, and mesh-use data via the PGMutagen C# wrapper.
 *
 * Handles initialization of the Mutagen library, enumeration of plugin language and record-type strings,
 * population of 3D model uses from loaded plugins, and saving of the generated output plugin.
 */
class PGPlugin {
private:
    static inline bool s_initialized = false;

public:
    enum class PluginLang : uint8_t {
        English,
        German,
        Italian,
        Spanish,
        SpanishMexico,
        French,
        Polish,
        PortugueseBrazil,
        Chinese,
        Russian,
        Japanese,
        Czech,
        Hungarian,
        Danish,
        Finnish,
        Greek,
        Norwegian,
        Swedish,
        Turkish,
        Arabic,
        Korean,
        Thai,
        ChineseSimplified,
    };

    static constexpr std::array<EnumStringHelper::EnumStringEntry<PluginLang>, 23> pluginLangTable {
        {
            { .value = PluginLang::Arabic, .name = "Arabic" },
            { .value = PluginLang::Chinese, .name = "Chinese" },
            { .value = PluginLang::ChineseSimplified, .name = "Chinese Simplified" },
            { .value = PluginLang::Czech, .name = "Czech" },
            { .value = PluginLang::Danish, .name = "Danish" },
            { .value = PluginLang::English, .name = "English" },
            { .value = PluginLang::Finnish, .name = "Finnish" },
            { .value = PluginLang::French, .name = "French" },
            { .value = PluginLang::German, .name = "German" },
            { .value = PluginLang::Greek, .name = "Greek" },
            { .value = PluginLang::Hungarian, .name = "Hungarian" },
            { .value = PluginLang::Italian, .name = "Italian" },
            { .value = PluginLang::Japanese, .name = "Japanese" },
            { .value = PluginLang::Korean, .name = "Korean" },
            { .value = PluginLang::Norwegian, .name = "Norwegian" },
            { .value = PluginLang::Polish, .name = "Polish" },
            { .value = PluginLang::PortugueseBrazil, .name = "Portuguese Brazil" },
            { .value = PluginLang::Russian, .name = "Russian" },
            { .value = PluginLang::Spanish, .name = "Spanish" },
            { .value = PluginLang::SpanishMexico, .name = "Spanish Mexico" },
            { .value = PluginLang::Swedish, .name = "Swedish" },
            { .value = PluginLang::Thai, .name = "Thai" },
            { .value = PluginLang::Turkish, .name = "Turkish" },
        },
    };

    enum class ESMMode : uint8_t {
        PGPatcherOnly = 0, // ESM flag only PGPatcher.esp (default)
        All = 1, // ESM flag all output plugins
        None = 2, // do not ESM flag any output plugin
    };

    enum class ModelRecordType : uint8_t {
        Activator, // ACTI
        Ammunition, // AMMO
        AnimatedObject, // ANIO
        Armor, // ARMO
        ArmorAddon, // ARMA
        ArtObject, // ARTO
        BodyPartData, // BPTD
        Book, // BOOK
        CameraShot, // CAMS
        Climate, // CLMT
        Container, // CONT
        Door, // DOOR
        Explosion, // EXPL
        Flora, // FLOR
        Furniture, // FURN
        Grass, // GRAS
        Hazard, // HAZD
        HeadPart, // HDPT
        IdleMarker, // IDLM
        Impact, // IPCT
        Ingestible, // ALCH
        Ingredient, // INGR
        Key, // KEYM
        LeveledNPC, // LVLN
        Light, // LIGH
        MaterialObject, // MATO
        MiscItem, // MISC
        MoveableStatic, // MSTT
        Projectile, // PROJ
        Scroll, // SCRL
        SoulGem, // SLGM
        StaticObject, // STAT
        TalkingActivator, // TACT
        Tree, // TREE
        Weapon, // WEAP
        Unknown,
    };

    static constexpr std::array<EnumStringHelper::EnumStringEntry<ModelRecordType>, 35> modelRecordTypeTable {
        {
            { .value = ModelRecordType::Activator, .name = "ACTI" },
            { .value = ModelRecordType::Ingestible, .name = "ALCH" },
            { .value = ModelRecordType::Ammunition, .name = "AMMO" },
            { .value = ModelRecordType::AnimatedObject, .name = "ANIO" },
            { .value = ModelRecordType::Armor, .name = "ARMO" },
            { .value = ModelRecordType::ArmorAddon, .name = "ARMA" },
            { .value = ModelRecordType::ArtObject, .name = "ARTO" },
            { .value = ModelRecordType::BodyPartData, .name = "BPTD" },
            { .value = ModelRecordType::Book, .name = "BOOK" },
            { .value = ModelRecordType::CameraShot, .name = "CAMS" },
            { .value = ModelRecordType::Climate, .name = "CLMT" },
            { .value = ModelRecordType::Container, .name = "CONT" },
            { .value = ModelRecordType::Door, .name = "DOOR" },
            { .value = ModelRecordType::Explosion, .name = "EXPL" },
            { .value = ModelRecordType::Flora, .name = "FLOR" },
            { .value = ModelRecordType::Furniture, .name = "FURN" },
            { .value = ModelRecordType::Grass, .name = "GRAS" },
            { .value = ModelRecordType::Hazard, .name = "HAZD" },
            { .value = ModelRecordType::HeadPart, .name = "HDPT" },
            { .value = ModelRecordType::IdleMarker, .name = "IDLM" },
            { .value = ModelRecordType::Impact, .name = "IPCT" },
            { .value = ModelRecordType::Ingredient, .name = "INGR" },
            { .value = ModelRecordType::Key, .name = "KEYM" },
            { .value = ModelRecordType::LeveledNPC, .name = "LVLN" },
            { .value = ModelRecordType::Light, .name = "LIGH" },
            { .value = ModelRecordType::MaterialObject, .name = "MATO" },
            { .value = ModelRecordType::MiscItem, .name = "MISC" },
            { .value = ModelRecordType::MoveableStatic, .name = "MSTT" },
            { .value = ModelRecordType::Projectile, .name = "PROJ" },
            { .value = ModelRecordType::Scroll, .name = "SCRL" },
            { .value = ModelRecordType::SoulGem, .name = "SLGM" },
            { .value = ModelRecordType::StaticObject, .name = "STAT" },
            { .value = ModelRecordType::TalkingActivator, .name = "TACT" },
            { .value = ModelRecordType::Tree, .name = "TREE" },
            { .value = ModelRecordType::Weapon, .name = "WEAP" },
        },
    };

    /**
     * @brief Stores attributes describing how a mesh record uses a 3D model.
     */
    struct MeshUseAttributes {
        /// @brief True if the model uses a weighted (body) variant requiring _0/_1 counterpart handling.
        bool isWeighted = false;
        /// @brief True if this is a single-pass MATO (Material Object) record.
        bool singlepassMATO = false;
        /// @brief True if this mesh is facegen
        bool isFacegen = false;
        /// @brief True if this mesh use should be excluded from patching.
        bool isIgnored = false;
        /// @brief True if this is a dummy use not actually tied to a plugin
        bool isDummyUse = false;
        /// @brief The plugin record type that references this model.
        ModelRecordType recType = ModelRecordType::Unknown;
        /// @brief Map from alternate texture set index to the overriding TextureSet.
        std::unordered_map<unsigned, PGTypes::TextureSet> alternateTextures;

        auto operator==(const MeshUseAttributes& other) const -> bool = default;
    };

    /**
     * @brief Converts a language string to the corresponding PluginLang enum value.
     *
     * @param lang String name of the language (e.g., "English").
     * @return Corresponding PluginLang value, defaulting to PluginLang::English if not found.
     */
    static auto getPluginLangFromString(const std::string& lang) -> PluginLang;

    /**
     * @brief Converts a PluginLang enum value to its display string.
     *
     * @param lang The PluginLang value.
     * @return String name (e.g., "English").
     */
    static auto getStringFromPluginLang(const PluginLang& lang) -> std::string;

    /**
     * @brief Returns a list of all available plugin language name strings.
     *
     * @return Vector of language name strings.
     */
    static auto getAvailablePluginLangStrs() -> std::vector<std::string>;

    /**
     * @brief Converts a record type string (e.g., "ACTI") to the corresponding ModelRecordType enum value.
     *
     * @param recTypeStr Four-letter record type code.
     * @return Corresponding ModelRecordType, or ModelRecordType::Unknown if not found.
     */
    static auto getRecTypeFromString(const std::string& recTypeStr) -> ModelRecordType;

    /**
     * @brief Converts a ModelRecordType enum value to its four-letter record type code string.
     *
     * @param recType The record type.
     * @return Four-letter code string, or empty string if unknown.
     */
    static auto getStringFromRecType(const ModelRecordType& recType) -> std::string;

    /**
     * @brief Returns a list of all available record type code strings.
     *
     * @return Vector of four-letter record type code strings.
     */
    static auto getAvailableRecTypeStrs() -> std::vector<std::string>;

    /**
     * @brief Returns the default set of ModelRecordType values that are enabled for patching.
     *
     * @return Unordered set of default-enabled ModelRecordType values.
     */
    static auto getDefaultRecTypeSet() -> std::unordered_set<ModelRecordType>;

    /**
     * @brief Initializes the PGMutagen library for the given game and language.
     *
     * Must be called before populateObjs(), getModelUses(), or savePlugin().
     *
     * @param game The BethesdaGame instance providing game type and data path.
     * @param exePath Path to the PGPatcher executable (for resolving relative paths).
     * @param lang Plugin language to use when reading localized strings (default: ENGLISH).
     */
    static void initialize(const BethesdaGame& game,
                           const std::filesystem::path& exePath,
                           const PluginLang& lang = PluginLang::English);

    /**
     * @brief Populates the internal object cache by reading all 3D model records from the loaded plugins.
     *
     * @param existingModPath Optional path to a pre-existing PGPatcher output plugin to merge with.
     */
    static void populateObjs(const std::filesystem::path& existingModPath = { });

    /**
     * @brief Resets plugin patching state to the baseline captured after populateObjs().
     */
    static void resetPatchingState();

    /**
     * @brief Returns all plugin records that reference the given model path.
     *
     * @param modelPath Wide-string relative model path (e.g., L"meshes\\foo\\bar.nif").
     * @return Vector of (FormKey, MeshUseAttributes) pairs, sorted with weighted entries first.
     */
    static auto getModelUses(const std::wstring& modelPath) -> std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                                                     MeshUseAttributes>>;

    /**
     * @brief Updates plugin records with the patched mesh paths from all committed mesh results.
     *
     * @param meshResults List of MeshResult objects produced by PGMeshPermutationTracker::saveMeshes().
     */
    static void setModelUses(const std::vector<PGMeshPermutationTracker::MeshResult>& meshResults);

    /**
     * @brief Saves the generated output plugin to the given directory.
     *
     * @param outputDir Directory in which to write the output plugin file.
     * @param esmMode Which output plugins to ESM flag.
     */
    static void savePlugin(const std::filesystem::path& outputDir,
                           ESMMode esmMode);

    /**
     * @brief Get the Plugin Path From Data Path object (removes textures or meshes from beginning of path)
     *
     * @param dataPath The data path to process
     * @return std::filesystem::path The plugin path derived from the data path, or the original path if it does not
     * start with "meshes" or "textures"
     */
    static auto getPluginPathFromDataPath(const std::filesystem::path& dataPath) -> std::filesystem::path;
};
