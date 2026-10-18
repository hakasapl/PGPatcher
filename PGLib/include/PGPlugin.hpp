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
#include <string_view>
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

    /// @brief Which output plugins get the ESM flag when they are saved.
    enum class ESMMode : uint8_t {
        PGPatcherOnly = 0, // ESM flag only PGPatcher.esp (default)
        All = 1, // ESM flag all output plugins
        None = 2, // do not ESM flag any output plugin
    };

    /// @brief Plugin record types that carry models, named after their xEdit signatures.
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
     * @brief What orders the uses of a mesh, see isUseOrderedBefore(). The strings are views into the use.
     */
    struct UseOrder {
        bool hasAlternateTextures = false;
        bool isWeighted = false;
        std::wstring_view modName;
        unsigned formID = 0;
        std::string_view subModel;
    };

    /**
     * @brief The order the uses of a mesh are patched in: uses without alternate textures first, weighted uses first,
     * then by plugin name, form ID and sub model. The first use of a mesh is the one that keeps the original path when
     * the uses patch differently, so the offline program and the PGLite runtime order them the same way.
     */
    [[nodiscard]] static bool isUseOrderedBefore(const UseOrder& a,
                                                 const UseOrder& b);

    /**
     * @brief Stores attributes describing how a mesh record uses a 3D model.
     */
    struct MeshUseAttributes {
        /// @brief True if the model uses a weighted (body) variant requiring _0/_1 counterpart handling.
        bool isWeighted = false;
        /// @brief True if this is a single-pass MATO (Material Object) record.
        bool isSinglepassMATO = false;
        /// @brief True if this mesh is facegen
        bool isFacegen = false;
        /// @brief True if this mesh is BodySlide ShapeData, which BodySlide builds its output meshes from
        bool isBodySlideShapeData = false;
        /// @brief True if this mesh use should be excluded from patching.
        bool isIgnored = false;
        /// @brief True if this is a dummy use not actually tied to a plugin
        bool isDummyUse = false;
        /// @brief The plugin record type that references this model.
        ModelRecordType recType = ModelRecordType::Unknown;
        /// @brief Map from alternate texture set index to the overriding TextureSet.
        std::unordered_map<unsigned, PGTypes::TextureSet> alternateTextures;

        bool operator==(const MeshUseAttributes& other) const = default;
    };

    /**
     * @brief Converts a language string to the corresponding PluginLang enum value.
     *
     * @param lang String name of the language (e.g., "English").
     * @return Corresponding PluginLang value, defaulting to PluginLang::English if not found.
     */
    static PluginLang pluginLangFromString(const std::string& lang);

    /**
     * @brief Converts a PluginLang enum value to its display string.
     *
     * @param lang The PluginLang value.
     * @return String name (e.g., "English").
     */
    static std::string stringFromPluginLang(const PluginLang& lang);

    /**
     * @brief Returns a list of all available plugin language name strings.
     *
     * @return Vector of language name strings.
     */
    static std::vector<std::string> availablePluginLangStrs();

    /**
     * @brief Converts a record type string (e.g., "ACTI") to the corresponding ModelRecordType enum value.
     *
     * @param recTypeStr Four-letter record type code.
     * @return Corresponding ModelRecordType, or ModelRecordType::Unknown if not found.
     */
    static ModelRecordType recTypeFromString(const std::string& recTypeStr);

    /**
     * @brief Converts a ModelRecordType enum value to its four-letter record type code string.
     *
     * @param recType The record type.
     * @return Four-letter code string, or empty string if unknown.
     */
    static std::string stringFromRecType(const ModelRecordType& recType);

    /**
     * @brief Returns a list of all available record type code strings.
     *
     * @return Vector of four-letter record type code strings.
     */
    static std::vector<std::string> availableRecTypeStrs();

    /**
     * @brief Returns the record types enabled for patching by default (every type except Unknown).
     */
    static std::unordered_set<ModelRecordType> defaultRecTypeSet();

    /**
     * @brief Returns the record types DynDOLOD scans for full models (ACTI, CONT, DOOR, FURN, MSTT, STAT, TREE and
     * GRAS): what the PGLite mode of the offline program patches, within the allowed record types.
     */
    static std::unordered_set<ModelRecordType> dynDOLODRecTypeSet();

    /**
     * @brief Initializes the PGMutagen library with the game's active load order.
     *
     * Must be called before populateObjs(), modelUses(), setModelUses() or savePlugin().
     *
     * @param game The BethesdaGame instance providing game type, data path and active plugins.
     * @param exePath Folder of the executable, where the .NET runtime resolves the PGMutagen assemblies.
     * @param lang Plugin language to use when reading localized strings (default: PluginLang::English).
     * @throws std::out_of_range for BethesdaGame::GameType::Unknown, std::runtime_error if the .NET runtime or the
     * plugin environment fails to load.
     */
    static void initialize(const BethesdaGame& game,
                           const std::filesystem::path& exePath,
                           const PluginLang& lang = PluginLang::English);

    /**
     * @brief Reads every model record of the load order (winning overrides) so modelUses() can answer, and captures the
     * baseline resetPatchingState() returns to.
     *
     * @param existingModPath Path of the PGPatcher.esp of a previous run, if any. Its texture sets are carried over so
     * texture sets created again get the same form IDs.
     * @throws std::runtime_error if initialize() was not called or the plugins cannot be read.
     */
    static void populateObjs(const std::filesystem::path& existingModPath = { });

    /**
     * @brief Resets plugin patching state to the baseline captured after populateObjs(). No-op before initialize().
     */
    static void resetPatchingState();

    /**
     * @brief Returns all plugin records that reference the given model path.
     *
     * @param modelPath Relative model path (e.g., L"meshes\\foo\\bar.nif"), compared without regard to case.
     * @return (FormKey, MeshUseAttributes) pairs: uses without alternate textures first, then weighted uses first, then
     * by plugin name, form ID and sub model. Empty before initialize().
     */
    static std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                 MeshUseAttributes>>
    modelUses(const std::wstring& modelPath);

    /**
     * @brief Updates the plugin records of every form key in the results with the output mesh path and the patched
     * alternate textures. Dummy uses (no plugin) are skipped. No-op before initialize().
     *
     * @param meshResults List of MeshResult objects produced by PGMeshPermutationTracker::saveMeshes().
     */
    static void setModelUses(const std::vector<PGMeshPermutationTracker::MeshResult>& meshResults);

    /**
     * @brief Updates the OBND (Object Bounds) of plugin records with the bounds computed for each mesh result's
     * primary model. Only TREE and GRAS records are updated: TexGen reads their OBND to decide which of them get LOD
     * billboards, while the OBND of other types is often authored differently from the geometry. Results without
     * bounds are skipped.
     *
     * @param meshResults List of MeshResult objects produced by PGMeshPermutationTracker::saveMeshes().
     */
    static void setObjectBounds(const std::vector<PGMeshPermutationTracker::MeshResult>& meshResults);

    /**
     * @brief Whether any record has been overridden so far, by setModelUses() or setObjectBounds(). A run whose only
     * changes are such overrides writes no file but the plugin.
     *
     * @return true if savePlugin() would write a plugin with an override. False before initialize().
     */
    [[nodiscard]] static bool hasModifiedRecords();

    /**
     * @brief Writes PGPatcher.esp (new texture sets) and PG_<n>.esp (overridden records, split by master count) to the
     * given directory. Nothing is written when no record changed.
     *
     * @param outputDir Directory in which to write the output plugin files.
     * @param esmMode Which output plugins to ESM flag.
     */
    static void savePlugin(const std::filesystem::path& outputDir,
                           ESMMode esmMode);

    /// @brief The stock records file of an output generated in PGLite mode, next to the plugins.
    static constexpr const wchar_t* stockRecordsFile = L"PGPatcher_Stock.json";

    /**
     * @brief Writes stockRecordsFile into the directory, after savePlugin(): for every record model the output plugins
     * changed, the stock model path and alternate textures and the written ones, for every texture set record
     * created, the stock texture set it replaced, and the textures the run generated, which PGLite leaves out of its
     * inputs. PGLite puts the records back from it when the output is enabled. The file is written even when no
     * record changed, since it marks the output as one PGLite accepts.
     *
     * @param outputDir Directory in which to write the file.
     * @param generatedTextures Lower case data relative paths of the textures the run generated.
     */
    static void saveStockRecords(const std::filesystem::path& outputDir,
                                 const std::vector<std::filesystem::path>& generatedTextures = { });

    /**
     * @brief Returns a data path the way plugins store it: without the leading "meshes" or "textures" folder
     *
     * @param dataPath Lower case path relative to the data folder
     * @return The path without its first component, or the path itself if that is neither "meshes" nor "textures"
     */
    static std::filesystem::path pluginPathFromDataPath(const std::filesystem::path& dataPath);
};
