#include "ParallaxGenPlugin.hpp"

#include "BethesdaGame.hpp"
#include "PGMutagenWrapper.hpp"
#include "util/EnumStringHelper.hpp"
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
    return EnumStringHelper::enumFromString(lang, PLUGINLANG_TABLE, PluginLang::ENGLISH);
}

auto ParallaxGenPlugin::getStringFromPluginLang(const PluginLang& lang) -> std::string
{
    return std::string(EnumStringHelper::stringFromEnum(lang, PLUGINLANG_TABLE, "English"));
}

auto ParallaxGenPlugin::getAvailablePluginLangStrs() -> vector<string>
{
    return EnumStringHelper::allEnumStrings(PLUGINLANG_TABLE);
}

auto ParallaxGenPlugin::getRecTypeFromString(const std::string& recTypeStr) -> ModelRecordType
{
    return EnumStringHelper::enumFromString(recTypeStr, MODEL_RECORD_TYPE_TABLE, ModelRecordType::UNKNOWN);
}

auto ParallaxGenPlugin::getStringFromRecType(const ModelRecordType& recType) -> std::string
{
    return std::string(EnumStringHelper::stringFromEnum(recType, MODEL_RECORD_TYPE_TABLE, "UNKNOWN"));
}

auto ParallaxGenPlugin::getAvailableRecTypeStrs() -> std::vector<std::string>
{
    return EnumStringHelper::allEnumStrings(MODEL_RECORD_TYPE_TABLE);
}

auto ParallaxGenPlugin::getDefaultRecTypeSet() -> std::unordered_set<ModelRecordType>
{
    // these are enabled by default in the initial config
    static const std::unordered_set<ModelRecordType> defaultSet = {
        ModelRecordType::ACTIVATOR,
        ModelRecordType::AMMUNITION,
        ModelRecordType::ANIMATED_OBJECT,
        ModelRecordType::ARMOR,
        ModelRecordType::ARMOR_ADDON,
        ModelRecordType::ART_OBJECT,
        ModelRecordType::BODY_PART_DATA,
        ModelRecordType::BOOK,
        ModelRecordType::CAMERA_SHOT,
        ModelRecordType::CLIMATE,
        ModelRecordType::CONTAINER,
        ModelRecordType::DOOR,
        ModelRecordType::EXPLOSION,
        ModelRecordType::FLORA,
        ModelRecordType::FURNITURE,
        ModelRecordType::GRASS,
        ModelRecordType::HAZARD,
        ModelRecordType::HEAD_PART,
        ModelRecordType::IDLE_MARKER,
        ModelRecordType::IMPACT,
        ModelRecordType::INGESTIBLE,
        ModelRecordType::INGREDIENT,
        ModelRecordType::KEY,
        ModelRecordType::LEVELED_NPC,
        ModelRecordType::LIGHT,
        ModelRecordType::MATERIAL_OBJECT,
        ModelRecordType::MISC_ITEM,
        ModelRecordType::MOVEABLE_STATIC,
        ModelRecordType::PROJECTILE,
        ModelRecordType::SCROLL,
        ModelRecordType::SOUL_GEM,
        ModelRecordType::STATIC_OBJECT,
        ModelRecordType::TALKING_ACTIVATOR,
        ModelRecordType::TREE,
        ModelRecordType::WEAPON,
    };

    return defaultSet;
}

void ParallaxGenPlugin::initialize(const BethesdaGame& game, const filesystem::path& exePath, const PluginLang& lang)
{
    // Maps BethesdaGame::GameType to Mutagen game type
    static const unordered_map<BethesdaGame::GameType, int> mutagenGameTypeMap
        = {{BethesdaGame::GameType::SKYRIM_SE, 2},
           {BethesdaGame::GameType::SKYRIM_VR, 3},
           {BethesdaGame::GameType::ENDERAL_SE, 6},
           {BethesdaGame::GameType::SKYRIM_GOG, 7}};

    PGMutagenWrapper::libInitialize(mutagenGameTypeMap.at(game.getGameType()),
                                    exePath,
                                    game.getGameDataPath().wstring(),
                                    game.getActivePlugins(),
                                    static_cast<unsigned int>(lang));

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
    // sort modelUses by putting weighted ones first, then by mod name, then by
    // formid, then by submodel
    std::ranges::sort(modelUses, [](const PGMutagenWrapper::ModelUse& a, const PGMutagenWrapper::ModelUse& b) -> bool {
        const bool aHasAltTex = !a.alternateTextures.empty();
        const bool bHasAltTex = !b.alternateTextures.empty();
        if (aHasAltTex != bHasAltTex) {
            return !aHasAltTex; // no alternate textures first
        }
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
            .modKey = modelUse.modName,
            .formID = modelUse.formID,
            .subMODL = modelUse.subModel,
        };
        MeshUseAttributes attributes;
        attributes.isWeighted = modelUse.isWeighted;
        attributes.singlepassMATO = modelUse.singlepassMATO;
        attributes.isIgnored = modelUse.isIgnored;
        attributes.recType = getRecTypeFromString(modelUse.type);

        for (const auto& altTex : modelUse.alternateTextures) {
            // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
            attributes.alternateTextures[altTex.slotID] = NIFUtil::TextureSet {
                altTex.slots[0],
                altTex.slots[1],
                altTex.slots[2],
                altTex.slots[3],
                altTex.slots[4],
                altTex.slots[5],
                altTex.slots[6],
                altTex.slots[7],
            };
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
