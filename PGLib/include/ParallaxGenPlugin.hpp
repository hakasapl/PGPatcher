#pragma once

#include "BethesdaGame.hpp"
#include "util/MeshTracker.hpp"
#include "util/NIFUtil.hpp"

#include <boost/algorithm/string/predicate.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

class ParallaxGenPlugin {
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

    struct MeshUseAttributes {
        bool isWeighted;
        bool singlepassMATO;
        bool isIgnored;
        ModelRecordType recType;
        std::unordered_map<unsigned int, NIFUtil::TextureSet> alternateTextures;
    };

    static auto getPluginLangFromString(const std::string& lang) -> PluginLang;
    static auto getStringFromPluginLang(const PluginLang& lang) -> std::string;
    static auto getAvailablePluginLangStrs() -> std::vector<std::string>;

    static auto getRecTypeFromString(const std::string& recTypeStr) -> ModelRecordType;
    static auto getStringFromRecType(const ModelRecordType& recType) -> std::string;
    static auto getAvailableRecTypeStrs() -> std::vector<std::string>;
    static auto getDefaultRecTypeSet() -> std::unordered_set<ModelRecordType>;

    static void initialize(
        const BethesdaGame& game, const std::filesystem::path& exePath, const PluginLang& lang = PluginLang::ENGLISH);

    static void populateObjs(const std::filesystem::path& existingModPath = {});

    static auto getModelUses(const std::wstring& modelPath)
        -> std::vector<std::pair<MeshTracker::FormKey, MeshUseAttributes>>;

    static void setModelUses(const std::vector<MeshTracker::MeshResult>& meshResults);

    static void savePlugin(const std::filesystem::path& outputDir, bool esmify);
};
