#include "patchers/base/PatcherMeshShader.hpp"

#include "NIFUtil.hpp"
#include "NifFile.hpp"
#include "ParallaxGenUtil.hpp"
#include <BasicTypes.hpp>
#include <Shaders.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

using namespace std;

// statics

unordered_map<tuple<filesystem::path, uint32_t>, PatcherMeshShader::PatchedTextureSet,
    PatcherMeshShader::PatchedTextureSetsHash>
    PatcherMeshShader::s_patchedTextureSets;
shared_mutex PatcherMeshShader::s_patchedTextureSetsMutex;

// Constructor
PatcherMeshShader::PatcherMeshShader(filesystem::path nifPath, nifly::NifFile* nif, string patcherName)
    : PatcherMesh(std::move(nifPath), nif, std::move(patcherName))
{
}

auto PatcherMeshShader::getTextureSet(const filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape)
    -> array<wstring, NUM_TEXTURE_SLOTS>
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto texturesetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));
    const auto nifShapeKey = make_tuple(nifPath, texturesetBlockID);

    // check if in patchedtexturesets
    const shared_lock lock(s_patchedTextureSetsMutex);
    if (s_patchedTextureSets.find(nifShapeKey) != s_patchedTextureSets.end()) {
        return s_patchedTextureSets[nifShapeKey].original;
    }

    // get the texture slots
    return NIFUtil::getTextureSlots(&nif, &nifShape);
}

auto PatcherMeshShader::setTextureSet(const filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape,
    const array<wstring, NUM_TEXTURE_SLOTS>& textures) -> bool
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto textureSetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));
    const auto nifShapeKey = make_tuple(nifPath, textureSetBlockID);

    bool patchedBefore = false;
    {
        const shared_lock lock(s_patchedTextureSetsMutex);
        patchedBefore = s_patchedTextureSets.find(nifShapeKey) != s_patchedTextureSets.end();
    }

    if (patchedBefore) {
        // This texture set has been patched before
        uint32_t newBlockID = 0;

        // already been patched, check if it is the same
        for (const auto& [possibleTexRecordID, possibleTextures] : s_patchedTextureSets[nifShapeKey].patchResults) {
            if (possibleTextures == textures) {
                newBlockID = possibleTexRecordID;

                if (newBlockID == textureSetBlockID) {
                    return false;
                }

                break;
            }
        }

        // Add a new texture set to the NIF
        if (newBlockID == 0) {
            auto newTextureSet = make_unique<nifly::BSShaderTextureSet>();
            newTextureSet->textures.resize(NUM_TEXTURE_SLOTS);
            for (uint32_t i = 0; i < textures.size(); i++) {
                newTextureSet->textures[i] = ParallaxGenUtil::utf16toASCII(textures.at(i));
            }

            // Set shader reference
            newBlockID = nif.GetHeader().AddBlock(std::move(newTextureSet));
        }

        auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
        const NiBlockRef<BSShaderTextureSet> newBlockRef(newBlockID);
        nifShaderBSLSP->textureSetRef = newBlockRef;

        {
            const unique_lock lock(s_patchedTextureSetsMutex);
            s_patchedTextureSets[nifShapeKey].patchResults[newBlockID] = textures;
        }
        return true;
    }

    const unique_lock lockWrite(s_patchedTextureSetsMutex);

    // set original for future use
    const auto slots = NIFUtil::getTextureSlots(&nif, &nifShape);
    s_patchedTextureSets[nifShapeKey].original = slots;

    // set the texture slots for the shape like normal
    const bool changed = NIFUtil::setTextureSlots(&nif, &nifShape, textures);

    // update the patchedtexturesets
    s_patchedTextureSets[nifShapeKey].patchResults[textureSetBlockID] = textures;

    return changed;
}
