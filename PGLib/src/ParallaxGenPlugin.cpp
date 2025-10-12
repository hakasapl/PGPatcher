#include "ParallaxGenPlugin.hpp"

#include "BethesdaGame.hpp"
#include "PGMutagenWrapper.hpp"
#include "util/MeshTracker.hpp"
#include "util/NIFUtil.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <string>
#include <unordered_map>
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

void ParallaxGenPlugin::initialize(const BethesdaGame& game, const filesystem::path& exePath, const PluginLang& lang)
{
    // Maps BethesdaGame::GameType to Mutagen game type
    static const unordered_map<BethesdaGame::GameType, int> mutagenGameTypeMap
        = { { BethesdaGame::GameType::SKYRIM, 1 }, { BethesdaGame::GameType::SKYRIM_SE, 2 },
              { BethesdaGame::GameType::SKYRIM_VR, 3 }, { BethesdaGame::GameType::ENDERAL, 5 },
              { BethesdaGame::GameType::ENDERAL_SE, 6 }, { BethesdaGame::GameType::SKYRIM_GOG, 7 } };

    PGMutagenWrapper::libInitialize(mutagenGameTypeMap.at(game.getGameType()), exePath,
        game.getGameDataPath().wstring(), game.getActivePlugins(), static_cast<unsigned int>(lang));
}

void ParallaxGenPlugin::populateObjs(const filesystem::path& existingModPath)
{
    PGMutagenWrapper::libPopulateObjs(existingModPath);
}

auto ParallaxGenPlugin::getModelUses(const std::wstring& modelPath)
    -> std::vector<std::pair<MeshTracker::FormKey, MeshUseAttributes>>
{
    vector<pair<MeshTracker::FormKey, MeshUseAttributes>> result;

    for (const auto& modelUse : PGMutagenWrapper::libGetModelUses(modelPath)) {
        const MeshTracker::FormKey formKey {
            .modKey = modelUse.modName, .formID = modelUse.formID, .subMODL = modelUse.subModel
        };
        MeshUseAttributes attributes;
        attributes.singlepassMATO = modelUse.singlepassMATO;

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
