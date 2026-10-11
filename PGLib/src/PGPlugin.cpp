#include "PGPlugin.hpp"

#include "PGMutagenWrapper.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/EnumStringHelper.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
void PGPlugin::initialize(const BethesdaGame& game,
                          const std::filesystem::path& exePath,
                          const PluginLang& lang)
{
    // Maps BethesdaGame::GameType to Mutagen game type
    static const std::unordered_map<BethesdaGame::GameType, int> mutagenGameTypeMap = {
        { BethesdaGame::GameType::SkyrimSE, 2 },
        { BethesdaGame::GameType::SkyrimVR, 3 },
        { BethesdaGame::GameType::EnderalSE, 6 },
        { BethesdaGame::GameType::SkyrimGOG, 7 },
    };

    PGMutagenWrapper::libInitialize(mutagenGameTypeMap.at(game.gameType()),
                                    exePath,
                                    game.gameDataPath().wstring(),
                                    game.activePlugins(),
                                    static_cast<unsigned>(lang));

    s_initialized = true;
}
void PGPlugin::populateObjs(const std::filesystem::path& existingModPath)
{
    PGMutagenWrapper::libPopulateObjs(existingModPath);
}
void PGPlugin::resetPatchingState()
{
    if (!s_initialized)
        return;

    PGMutagenWrapper::libResetPatchingState();
}
auto PGPlugin::modelUses(const std::wstring& modelPath) -> std::vector<std::pair<PGMeshPermutationTracker::FormKey,
                                                                                 MeshUseAttributes>>
{
    std::vector<std::pair<PGMeshPermutationTracker::FormKey, MeshUseAttributes>> result;

    if (!s_initialized)
        return { };

    auto modelUses = PGMutagenWrapper::libGetModelUses(modelPath);
    // Sort modelUses by putting weighted ones first, then by mod name, then by
    // formid, then by submodel.
    std::ranges::sort(modelUses, [](const PGMutagenWrapper::ModelUse& a, const PGMutagenWrapper::ModelUse& b) {
        const bool aHasAltTex = !a.alternateTextures.empty();
        const bool bHasAltTex = !b.alternateTextures.empty();
        if (aHasAltTex != bHasAltTex)
            return !aHasAltTex; // no alternate textures first
        if (a.isWeighted != b.isWeighted)
            return a.isWeighted > b.isWeighted; // weighted first
        if (a.modName != b.modName)
            return a.modName < b.modName; // alphabetical mod name
        if (a.formID != b.formID)
            return a.formID < b.formID; // ascending formid
        return a.subModel < b.subModel; // alphabetical submodel
    });

    for (const auto& modelUse : modelUses) {
        const PGMeshPermutationTracker::FormKey formKey {
            .modKey = modelUse.modName,
            .formID = modelUse.formID,
            .subMODL = modelUse.subModel,
        };
        MeshUseAttributes attributes;
        attributes.isWeighted = modelUse.isWeighted;
        attributes.isSinglepassMATO = modelUse.isSinglepassMATO;
        attributes.isIgnored = modelUse.isIgnored;
        attributes.isDummyUse = false;
        attributes.recType = recTypeFromString(modelUse.type);

        for (const auto& altTex : modelUse.alternateTextures) {
            attributes.alternateTextures[altTex.slotID] = PGTypes::TextureSet {
                altTex.slots[0], altTex.slots[1], altTex.slots[2], altTex.slots[3],
                altTex.slots[4], altTex.slots[5], altTex.slots[6], altTex.slots[7],
            };
        }

        result.emplace_back(formKey, attributes);
    }

    return result;
}
void PGPlugin::setModelUses(const std::vector<PGMeshPermutationTracker::MeshResult>& meshResults)
{
    if (!s_initialized)
        return;

    std::vector<PGMutagenWrapper::ModelUse> modelUses;

    for (const auto& meshResult : meshResults) {
        for (const auto& [formKey, altTexMap] : meshResult.altTexResults) {
            if (formKey.modKey.empty() || !formKey.formID) {
                // Skip dummy use.
                continue;
            }

            PGMutagenWrapper::ModelUse modelUse;
            modelUse.modName = formKey.modKey;
            modelUse.formID = formKey.formID;
            modelUse.subModel = formKey.subMODL;
            modelUse.meshFile = meshResult.meshPath.wstring();

            const auto idxCorr = meshResult.idxCorrections;

            for (const auto& [slotID, textureSet] : altTexMap) {
                PGMutagenWrapper::AlternateTexture altTex;
                altTex.slotID = static_cast<int>(slotID);
                if (idxCorr.contains(altTex.slotID))
                    altTex.slotIDNew = idxCorr.at(altTex.slotID);
                else
                    altTex.slotIDNew = altTex.slotID; // No change

                altTex.slots[0] = textureSet[0];
                altTex.slots[1] = textureSet[1];
                altTex.slots[2] = textureSet[2];
                altTex.slots[3] = textureSet[3];
                altTex.slots[4] = textureSet[4];
                altTex.slots[5] = textureSet[5];
                altTex.slots[6] = textureSet[6];
                altTex.slots[7] = textureSet[7];

                modelUse.alternateTextures.push_back(altTex);
            }

            modelUses.push_back(modelUse);
        }
    }

    PGMutagenWrapper::libSetModelUses(modelUses);
}
void PGPlugin::savePlugin(const std::filesystem::path& outputDir,
                          ESMMode esmMode)
{
    PGMutagenWrapper::libFinalize(outputDir, static_cast<int>(esmMode));
    // FIXME: Add to the generated files.
}
