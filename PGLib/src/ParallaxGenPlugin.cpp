#include "ParallaxGenPlugin.hpp"

#include "BethesdaGame.hpp"
#include "PGMutagenWrapper.hpp"
#include "util/MeshTracker.hpp"
#include "util/NIFUtil.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace std;

auto ParallaxGenPlugin::getPluginLangFromString(const std::string& lang) -> PluginLang
{
    static const unordered_map<string, PluginLang> langMap = { { "English", PluginLang::ENGLISH },
        { "German", PluginLang::GERMAN }, { "Italian", PluginLang::ITALIAN }, { "Spanish", PluginLang::SPANISH },
        { "Spanish Mexico", PluginLang::SPANISH_MEXICO }, { "French", PluginLang::FRENCH },
        { "Polish", PluginLang::POLISH }, { "Portuguese Brazil", PluginLang::PORTUGUESE_BRAZIL },
        { "Chinese", PluginLang::CHINESE }, { "Russian", PluginLang::RUSSIAN }, { "Japanese", PluginLang::JAPANESE },
        { "Czech", PluginLang::CZECH }, { "Hungarian", PluginLang::HUNGARIAN }, { "Danish", PluginLang::DANISH },
        { "Finnish", PluginLang::FINNISH }, { "Greek", PluginLang::GREEK }, { "Norwegian", PluginLang::NORWEGIAN },
        { "Swedish", PluginLang::SWEDISH }, { "Turkish", PluginLang::TURKISH }, { "Arabic", PluginLang::ARABIC },
        { "Korean", PluginLang::KOREAN }, { "Thai", PluginLang::THAI },
        { "Chinese Simplified", PluginLang::CHINESE_SIMPLIFIED } };

    if (langMap.contains(lang)) {
        return langMap.at(lang);
    }

    return PluginLang::ENGLISH; // Default to English if not found
}

auto ParallaxGenPlugin::getStringFromPluginLang(const PluginLang& lang) -> std::string
{
    static const unordered_map<PluginLang, string> langStringMap = { { PluginLang::ENGLISH, "English" },
        { PluginLang::GERMAN, "German" }, { PluginLang::ITALIAN, "Italian" }, { PluginLang::SPANISH, "Spanish" },
        { PluginLang::SPANISH_MEXICO, "Spanish Mexico" }, { PluginLang::FRENCH, "French" },
        { PluginLang::POLISH, "Polish" }, { PluginLang::PORTUGUESE_BRAZIL, "Portuguese Brazil" },
        { PluginLang::CHINESE, "Chinese" }, { PluginLang::RUSSIAN, "Russian" }, { PluginLang::JAPANESE, "Japanese" },
        { PluginLang::CZECH, "Czech" }, { PluginLang::HUNGARIAN, "Hungarian" }, { PluginLang::DANISH, "Danish" },
        { PluginLang::FINNISH, "Finnish" }, { PluginLang::GREEK, "Greek" }, { PluginLang::NORWEGIAN, "Norwegian" },
        { PluginLang::SWEDISH, "Swedish" }, { PluginLang::TURKISH, "Turkish" }, { PluginLang::ARABIC, "Arabic" },
        { PluginLang::KOREAN, "Korean" }, { PluginLang::THAI, "Thai" },
        { PluginLang::CHINESE_SIMPLIFIED, "Chinese Simplified" } };

    if (langStringMap.contains(lang)) {
        return langStringMap.at(lang);
    }

    return langStringMap.at(PluginLang::ENGLISH); // Default to English if not found
}

auto ParallaxGenPlugin::getAvailablePluginLangStrs() -> vector<string>
{
    // alphabetical vector
    static const vector<string> langStrs = { "Arabic", "Chinese", "Chinese Simplified", "Czech", "Danish", "English",
        "Finnish", "French", "German", "Greek", "Hungarian", "Italian", "Japanese", "Korean", "Norwegian", "Polish",
        "Portuguese Brazil", "Russian", "Spanish", "Spanish Mexico", "Swedish", "Thai", "Turkish" };

    return langStrs;
}

auto ParallaxGenPlugin::getRecTypeFromString(const std::string& recTypeStr) -> ModelRecordType
{
    static const std::unordered_map<std::string, ModelRecordType> typeMap = { { "ACTI", ModelRecordType::ACTIVATOR },
        { "AMMO", ModelRecordType::AMMUNITION }, { "ANIO", ModelRecordType::ANIMATED_OBJECT },
        { "ARMO", ModelRecordType::ARMOR }, { "ARMA", ModelRecordType::ARMOR_ADDON },
        { "ARTO", ModelRecordType::ART_OBJECT }, { "BPTD", ModelRecordType::BODY_PART_DATA },
        { "BOOK", ModelRecordType::BOOK }, { "CAMS", ModelRecordType::CAMERA_SHOT },
        { "CLMT", ModelRecordType::CLIMATE }, { "CONT", ModelRecordType::CONTAINER }, { "DOOR", ModelRecordType::DOOR },
        { "EXPL", ModelRecordType::EXPLOSION }, { "FLOR", ModelRecordType::FLORA },
        { "FURN", ModelRecordType::FURNITURE }, { "GRAS", ModelRecordType::GRASS }, { "HAZD", ModelRecordType::HAZARD },
        { "HDPT", ModelRecordType::HEAD_PART }, { "IDLM", ModelRecordType::IDLE_MARKER },
        { "IPCT", ModelRecordType::IMPACT }, { "ALCH", ModelRecordType::INGESTIBLE },
        { "INGR", ModelRecordType::INGREDIENT }, { "KEYM", ModelRecordType::KEY },
        { "LVLN", ModelRecordType::LEVELED_NPC }, { "LIGH", ModelRecordType::LIGHT },
        { "MATO", ModelRecordType::MATERIAL_OBJECT }, { "MISC", ModelRecordType::MISC_ITEM },
        { "MSTT", ModelRecordType::MOVEABLE_STATIC }, { "PROJ", ModelRecordType::PROJECTILE },
        { "SCRL", ModelRecordType::SCROLL }, { "SLGM", ModelRecordType::SOUL_GEM },
        { "STAT", ModelRecordType::STATIC_OBJECT }, { "TACT", ModelRecordType::TALKING_ACTIVATOR },
        { "TREE", ModelRecordType::TREE }, { "WEAP", ModelRecordType::WEAPON } };

    if (auto it = typeMap.find(recTypeStr); it != typeMap.end()) {
        return it->second;
    }

    return ModelRecordType::UNKNOWN; // Default
}

auto ParallaxGenPlugin::getStringFromRecType(const ModelRecordType& recType) -> std::string
{
    static const std::unordered_map<ModelRecordType, std::string> typeStringMap = { { ModelRecordType::ACTIVATOR,
                                                                                        "ACTI" },
        { ModelRecordType::AMMUNITION, "AMMO" }, { ModelRecordType::ANIMATED_OBJECT, "ANIO" },
        { ModelRecordType::ARMOR, "ARMO" }, { ModelRecordType::ARMOR_ADDON, "ARMA" },
        { ModelRecordType::ART_OBJECT, "ARTO" }, { ModelRecordType::BODY_PART_DATA, "BPTD" },
        { ModelRecordType::BOOK, "BOOK" }, { ModelRecordType::CAMERA_SHOT, "CAMS" },
        { ModelRecordType::CLIMATE, "CLMT" }, { ModelRecordType::CONTAINER, "CONT" }, { ModelRecordType::DOOR, "DOOR" },
        { ModelRecordType::EXPLOSION, "EXPL" }, { ModelRecordType::FLORA, "FLOR" },
        { ModelRecordType::FURNITURE, "FURN" }, { ModelRecordType::GRASS, "GRAS" }, { ModelRecordType::HAZARD, "HAZD" },
        { ModelRecordType::HEAD_PART, "HDPT" }, { ModelRecordType::IDLE_MARKER, "IDLM" },
        { ModelRecordType::IMPACT, "IPCT" }, { ModelRecordType::INGESTIBLE, "ALCH" },
        { ModelRecordType::INGREDIENT, "INGR" }, { ModelRecordType::KEY, "KEYM" },
        { ModelRecordType::LEVELED_NPC, "LVLN" }, { ModelRecordType::LIGHT, "LIGH" },
        { ModelRecordType::MATERIAL_OBJECT, "MATO" }, { ModelRecordType::MISC_ITEM, "MISC" },
        { ModelRecordType::MOVEABLE_STATIC, "MSTT" }, { ModelRecordType::PROJECTILE, "PROJ" },
        { ModelRecordType::SCROLL, "SCRL" }, { ModelRecordType::SOUL_GEM, "SLGM" },
        { ModelRecordType::STATIC_OBJECT, "STAT" }, { ModelRecordType::TALKING_ACTIVATOR, "TACT" },
        { ModelRecordType::TREE, "TREE" }, { ModelRecordType::WEAPON, "WEAP" } };

    if (auto it = typeStringMap.find(recType); it != typeStringMap.end()) {
        return it->second;
    }

    return typeStringMap.at(ModelRecordType::ACTIVATOR); // Default
}

auto ParallaxGenPlugin::getAvailableRecTypeStrs() -> std::vector<std::string>
{
    static const std::vector<std::string> typeStrs = { "ACTI", "ALCH", "AMMO", "ANIO", "ARMA", "ARMO", "ARTO", "BOOK",
        "BPTD", "CAMS", "CLMT", "CONT", "DOOR", "EXPL", "FLOR", "FURN", "GRAS", "HAZD", "HDPT", "IDLM", "INGR", "IPCT",
        "KEYM", "LIGH", "LVLN", "MATO", "MISC", "MSTT", "PROJ", "SCRL", "SLGM", "STAT", "TACT", "TREE", "WEAP" };

    return typeStrs;
}

auto ParallaxGenPlugin::getDefaultRecTypeSet() -> std::unordered_set<ModelRecordType>
{
    static const std::unordered_set<ModelRecordType> defaultSet = { ModelRecordType::ACTIVATOR,
        ModelRecordType::AMMUNITION, ModelRecordType::ANIMATED_OBJECT, ModelRecordType::ARMOR,
        ModelRecordType::ARMOR_ADDON, ModelRecordType::ART_OBJECT, ModelRecordType::BODY_PART_DATA,
        ModelRecordType::BOOK, ModelRecordType::CAMERA_SHOT, ModelRecordType::CLIMATE, ModelRecordType::CONTAINER,
        ModelRecordType::DOOR, ModelRecordType::EXPLOSION, ModelRecordType::FLORA, ModelRecordType::FURNITURE,
        ModelRecordType::GRASS, ModelRecordType::HAZARD, ModelRecordType::HEAD_PART, ModelRecordType::IDLE_MARKER,
        ModelRecordType::IMPACT, ModelRecordType::INGESTIBLE, ModelRecordType::INGREDIENT, ModelRecordType::KEY,
        ModelRecordType::LEVELED_NPC, ModelRecordType::LIGHT, ModelRecordType::MATERIAL_OBJECT,
        ModelRecordType::MISC_ITEM, ModelRecordType::MOVEABLE_STATIC, ModelRecordType::PROJECTILE,
        ModelRecordType::SCROLL, ModelRecordType::SOUL_GEM, ModelRecordType::STATIC_OBJECT,
        ModelRecordType::TALKING_ACTIVATOR, ModelRecordType::TREE, ModelRecordType::WEAPON };

    return defaultSet;
}

void ParallaxGenPlugin::initialize(const BethesdaGame& game, const filesystem::path& exePath, const PluginLang& lang)
{
    // Maps BethesdaGame::GameType to Mutagen game type
    static const unordered_map<BethesdaGame::GameType, int> mutagenGameTypeMap
        = { { BethesdaGame::GameType::SKYRIM_SE, 2 }, { BethesdaGame::GameType::SKYRIM_VR, 3 },
              { BethesdaGame::GameType::ENDERAL_SE, 6 }, { BethesdaGame::GameType::SKYRIM_GOG, 7 } };

    PGMutagenWrapper::libInitialize(mutagenGameTypeMap.at(game.getGameType()), exePath,
        game.getGameDataPath().wstring(), game.getActivePlugins(), static_cast<unsigned int>(lang));

    s_initialized = true;
}

void ParallaxGenPlugin::populateObjs(const filesystem::path& existingModPath)
{
    PGMutagenWrapper::libPopulateObjs(existingModPath);
}

auto ParallaxGenPlugin::getModelUses(const std::wstring& modelPath)
    -> std::vector<std::pair<MeshTracker::FormKey, MeshUseAttributes>>
{
    vector<pair<MeshTracker::FormKey, MeshUseAttributes>> result;

    if (!s_initialized) {
        return {};
    }

    auto modelUses = PGMutagenWrapper::libGetModelUses(modelPath);
    // sort modelUses by putting weighted ones first, then by mod name, then by formid, then by submodel
    std::ranges::sort(modelUses, [](const PGMutagenWrapper::ModelUse& a, const PGMutagenWrapper::ModelUse& b) -> bool {
        if (a.isWeighted != b.isWeighted) {
            return a.isWeighted > b.isWeighted; // weighted first
        }
        if (a.modName != b.modName) {
            return a.modName < b.modName; // alphabetical mod name
        }
        if (a.formID != b.formID) {
            return a.formID < b.formID; // ascending formid
        }
        return a.subModel < b.subModel; // alphabetical submodel
    });

    for (const auto& modelUse : modelUses) {
        const MeshTracker::FormKey formKey {
            .modKey = modelUse.modName, .formID = modelUse.formID, .subMODL = modelUse.subModel
        };
        MeshUseAttributes attributes;
        attributes.isWeighted = modelUse.isWeighted;
        attributes.singlepassMATO = modelUse.singlepassMATO;
        attributes.isIgnored = modelUse.isIgnored;
        attributes.recType = getRecTypeFromString(modelUse.type);

        for (const auto& altTex : modelUse.alternateTextures) {
            // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
            attributes.alternateTextures[altTex.slotID] = NIFUtil::TextureSet { altTex.slots[0], altTex.slots[1],
                altTex.slots[2], altTex.slots[3], altTex.slots[4], altTex.slots[5], altTex.slots[6], altTex.slots[7] };
            // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
        }

        result.emplace_back(formKey, attributes);
    }

    return result;
}

void ParallaxGenPlugin::setModelUses(const std::vector<MeshTracker::MeshResult>& meshResults)
{
    if (!s_initialized) {
        return;
    }

    vector<PGMutagenWrapper::ModelUse> modelUses;

    for (const auto& meshResult : meshResults) {
        for (const auto& [formKey, altTexMap] : meshResult.altTexResults) {
            PGMutagenWrapper::ModelUse modelUse;
            modelUse.modName = formKey.modKey;
            modelUse.formID = formKey.formID;
            modelUse.subModel = formKey.subMODL;
            modelUse.meshFile = meshResult.meshPath.wstring();

            const auto idxCorr = meshResult.idxCorrections;

            for (const auto& [slotID, textureSet] : altTexMap) {
                PGMutagenWrapper::AlternateTexture altTex;
                altTex.slotID = static_cast<int>(slotID);
                if (idxCorr.contains(altTex.slotID)) {
                    altTex.slotIDNew = idxCorr.at(altTex.slotID);
                } else {
                    altTex.slotIDNew = altTex.slotID; // No change
                }

                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                altTex.slots[0] = textureSet[0];
                altTex.slots[1] = textureSet[1];
                altTex.slots[2] = textureSet[2];
                altTex.slots[3] = textureSet[3];
                altTex.slots[4] = textureSet[4];
                altTex.slots[5] = textureSet[5];
                altTex.slots[6] = textureSet[6];
                altTex.slots[7] = textureSet[7];
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

                modelUse.alternateTextures.push_back(altTex);
            }

            modelUses.push_back(modelUse);
        }
    }

    PGMutagenWrapper::libSetModelUses(modelUses);
}

void ParallaxGenPlugin::savePlugin(const filesystem::path& outputDir, bool esmify)
{
    PGMutagenWrapper::libFinalize(outputDir, esmify);
    // TODO add to generated files
}
