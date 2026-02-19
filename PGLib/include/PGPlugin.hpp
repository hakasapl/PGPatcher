#pragma once

#include "common/BethesdaGame.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
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

class PGPlugin {
private:
    static inline bool s_initialized = false;

public:
    enum class PluginLang : uint8_t {
        ENGLISH,
        GERMAN,
        ITALIAN,
        SPANISH,
        SPANISH_MEXICO,
        FRENCH,
        POLISH,
        PORTUGUESE_BRAZIL,
        CHINESE,
        RUSSIAN,
        JAPANESE,
        CZECH,
        HUNGARIAN,
        DANISH,
        FINNISH,
        GREEK,
        NORWEGIAN,
        SWEDISH,
        TURKISH,
        ARABIC,
        KOREAN,
        THAI,
        CHINESE_SIMPLIFIED
    };

    static constexpr std::array<EnumStringHelper::EnumStringEntry<PluginLang>, 23> PLUGINLANG_TABLE {{
        {.value = PluginLang::ARABIC, .name = "Arabic"},
        {.value = PluginLang::CHINESE, .name = "Chinese"},
        {.value = PluginLang::CHINESE_SIMPLIFIED, .name = "Chinese Simplified"},
        {.value = PluginLang::CZECH, .name = "Czech"},
        {.value = PluginLang::DANISH, .name = "Danish"},
        {.value = PluginLang::ENGLISH, .name = "English"},
        {.value = PluginLang::FINNISH, .name = "Finnish"},
        {.value = PluginLang::FRENCH, .name = "French"},
        {.value = PluginLang::GERMAN, .name = "German"},
        {.value = PluginLang::GREEK, .name = "Greek"},
        {.value = PluginLang::HUNGARIAN, .name = "Hungarian"},
        {.value = PluginLang::ITALIAN, .name = "Italian"},
        {.value = PluginLang::JAPANESE, .name = "Japanese"},
        {.value = PluginLang::KOREAN, .name = "Korean"},
        {.value = PluginLang::NORWEGIAN, .name = "Norwegian"},
        {.value = PluginLang::POLISH, .name = "Polish"},
        {.value = PluginLang::PORTUGUESE_BRAZIL, .name = "Portuguese Brazil"},
        {.value = PluginLang::RUSSIAN, .name = "Russian"},
        {.value = PluginLang::SPANISH, .name = "Spanish"},
        {.value = PluginLang::SPANISH_MEXICO, .name = "Spanish Mexico"},
        {.value = PluginLang::SWEDISH, .name = "Swedish"},
        {.value = PluginLang::THAI, .name = "Thai"},
        {.value = PluginLang::TURKISH, .name = "Turkish"},
    }};

    enum class ModelRecordType : uint8_t {
        ACTIVATOR, // ACTI
        AMMUNITION, // AMMO
        ANIMATED_OBJECT, // ANIO
        ARMOR, // ARMO
        ARMOR_ADDON, // ARMA
        ART_OBJECT, // ARTO
        BODY_PART_DATA, // BPTD
        BOOK, // BOOK
        CAMERA_SHOT, // CAMS
        CLIMATE, // CLMT
        CONTAINER, // CONT
        DOOR, // DOOR
        EXPLOSION, // EXPL
        FLORA, // FLOR
        FURNITURE, // FURN
        GRASS, // GRAS
        HAZARD, // HAZD
        HEAD_PART, // HDPT
        IDLE_MARKER, // IDLM
        IMPACT, // IPCT
        INGESTIBLE, // ALCH
        INGREDIENT, // INGR
        KEY, // KEYM
        LEVELED_NPC, // LVLN
        LIGHT, // LIGH
        MATERIAL_OBJECT, // MATO
        MISC_ITEM, // MISC
        MOVEABLE_STATIC, // MSTT
        PROJECTILE, // PROJ
        SCROLL, // SCRL
        SOUL_GEM, // SLGM
        STATIC_OBJECT, // STAT
        TALKING_ACTIVATOR, // TACT
        TREE, // TREE
        WEAPON, // WEAP
        UNKNOWN
    };

    static constexpr std::array<EnumStringHelper::EnumStringEntry<ModelRecordType>, 35> MODEL_RECORD_TYPE_TABLE {{
        {.value = ModelRecordType::ACTIVATOR, .name = "ACTI"},
        {.value = ModelRecordType::INGESTIBLE, .name = "ALCH"},
        {.value = ModelRecordType::AMMUNITION, .name = "AMMO"},
        {.value = ModelRecordType::ANIMATED_OBJECT, .name = "ANIO"},
        {.value = ModelRecordType::ARMOR, .name = "ARMO"},
        {.value = ModelRecordType::ARMOR_ADDON, .name = "ARMA"},
        {.value = ModelRecordType::ART_OBJECT, .name = "ARTO"},
        {.value = ModelRecordType::BODY_PART_DATA, .name = "BPTD"},
        {.value = ModelRecordType::BOOK, .name = "BOOK"},
        {.value = ModelRecordType::CAMERA_SHOT, .name = "CAMS"},
        {.value = ModelRecordType::CLIMATE, .name = "CLMT"},
        {.value = ModelRecordType::CONTAINER, .name = "CONT"},
        {.value = ModelRecordType::DOOR, .name = "DOOR"},
        {.value = ModelRecordType::EXPLOSION, .name = "EXPL"},
        {.value = ModelRecordType::FLORA, .name = "FLOR"},
        {.value = ModelRecordType::FURNITURE, .name = "FURN"},
        {.value = ModelRecordType::GRASS, .name = "GRAS"},
        {.value = ModelRecordType::HAZARD, .name = "HAZD"},
        {.value = ModelRecordType::HEAD_PART, .name = "HDPT"},
        {.value = ModelRecordType::IDLE_MARKER, .name = "IDLM"},
        {.value = ModelRecordType::IMPACT, .name = "IPCT"},
        {.value = ModelRecordType::INGREDIENT, .name = "INGR"},
        {.value = ModelRecordType::KEY, .name = "KEYM"},
        {.value = ModelRecordType::LEVELED_NPC, .name = "LVLN"},
        {.value = ModelRecordType::LIGHT, .name = "LIGH"},
        {.value = ModelRecordType::MATERIAL_OBJECT, .name = "MATO"},
        {.value = ModelRecordType::MISC_ITEM, .name = "MISC"},
        {.value = ModelRecordType::MOVEABLE_STATIC, .name = "MSTT"},
        {.value = ModelRecordType::PROJECTILE, .name = "PROJ"},
        {.value = ModelRecordType::SCROLL, .name = "SCRL"},
        {.value = ModelRecordType::SOUL_GEM, .name = "SLGM"},
        {.value = ModelRecordType::STATIC_OBJECT, .name = "STAT"},
        {.value = ModelRecordType::TALKING_ACTIVATOR, .name = "TACT"},
        {.value = ModelRecordType::TREE, .name = "TREE"},
        {.value = ModelRecordType::WEAPON, .name = "WEAP"},
    }};

    struct MeshUseAttributes {
        bool isWeighted;
        bool singlepassMATO;
        bool isIgnored;
        ModelRecordType recType;
        std::unordered_map<unsigned int, PGTypes::TextureSet> alternateTextures;
    };

    static auto getPluginLangFromString(const std::string& lang) -> PluginLang;
    static auto getStringFromPluginLang(const PluginLang& lang) -> std::string;
    static auto getAvailablePluginLangStrs() -> std::vector<std::string>;

    static auto getRecTypeFromString(const std::string& recTypeStr) -> ModelRecordType;
    static auto getStringFromRecType(const ModelRecordType& recType) -> std::string;
    static auto getAvailableRecTypeStrs() -> std::vector<std::string>;
    static auto getDefaultRecTypeSet() -> std::unordered_set<ModelRecordType>;

    static void initialize(const BethesdaGame& game,
                           const std::filesystem::path& exePath,
                           const PluginLang& lang = PluginLang::ENGLISH);

    static void populateObjs(const std::filesystem::path& existingModPath = {});

    static auto getModelUses(const std::wstring& modelPath) -> std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                                                     MeshUseAttributes>>;

    static void setModelUses(const std::vector<PGMeshPermutationTracker::MeshResult>& meshResults);

    static void savePlugin(const std::filesystem::path& outputDir,
                           bool esmify);

    /**
     * @brief Get the Plugin Path From Data Path object (removes textures or meshes from beginning of path)
     *
     * @param dataPath The data path to process
     * @return std::filesystem::path The plugin path derived from the data path, or the original path if it does not
     * start with "meshes" or "textures"
     */
    static auto getPluginPathFromDataPath(const std::filesystem::path& dataPath) -> std::filesystem::path;
};
