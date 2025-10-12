#include "patchers/base/PatcherMesh.hpp"

#include "patchers/base/Patcher.hpp"
#include "util/NIFUtil.hpp"
#include "util/ParallaxGenUtil.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

using namespace std;

// statics

unordered_map<filesystem::path, unordered_map<uint32_t, PatcherMesh::PatchedTextureSet>>
    PatcherMesh::s_patchedTextureSets;
shared_mutex PatcherMesh::s_patchedTextureSetsMutex;

auto PatcherMesh::getTextureSet(const filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape)
    -> array<wstring, NUM_TEXTURE_SLOTS>
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto texturesetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));

    // check if in patchedtexturesets
    const shared_lock lock(s_patchedTextureSetsMutex);
    if (s_patchedTextureSets.contains(nifPath) && s_patchedTextureSets.at(nifPath).contains(texturesetBlockID)) {
        return s_patchedTextureSets.at(nifPath).at(texturesetBlockID).original;
    }

    // get the texture slots
    return NIFUtil::getTextureSlots(&nif, &nifShape);
}

auto PatcherMesh::setTextureSet(const filesystem::path& nifPath, nifly::NifFile& nif, nifly::NiShape& nifShape,
    const array<wstring, NUM_TEXTURE_SLOTS>& textures) -> bool
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto textureSetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));

    bool patchedBefore = false;
    {
        const shared_lock lock(s_patchedTextureSetsMutex);
        patchedBefore
            = s_patchedTextureSets.contains(nifPath) && s_patchedTextureSets.at(nifPath).contains(textureSetBlockID);
    }

    if (patchedBefore) {
        // This texture set has been patched before
        uint32_t newBlockID = 0;

        {
            const shared_lock lockAgain(s_patchedTextureSetsMutex);
            const auto& patchResults = s_patchedTextureSets.at(nifPath).at(textureSetBlockID).patchResults;

            // already been patched, check if it is the same
            for (const auto& [possibleTexRecordID, possibleTextures] : patchResults) {
                if (possibleTextures == textures) {
                    newBlockID = possibleTexRecordID;

                    if (newBlockID == textureSetBlockID) {
                        return false;
                    }

                    break;
                }
            }
        }

        // Add a new texture set to the NIF
        if (newBlockID == 0) {
            auto newTextureSet = std::make_unique<nifly::BSShaderTextureSet>();
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
            s_patchedTextureSets[nifPath][textureSetBlockID].patchResults[newBlockID] = textures;
        }
        return true;
    }

    const unique_lock lockWrite(s_patchedTextureSetsMutex);

    // set original for future use
    const auto slots = NIFUtil::getTextureSlots(&nif, &nifShape);
    s_patchedTextureSets[nifPath][textureSetBlockID].original = slots;

    // set the texture slots for the shape like normal
    const bool changed = NIFUtil::setTextureSlots(&nif, &nifShape, textures);

    // update the patchedtexturesets
    s_patchedTextureSets[nifPath][textureSetBlockID].patchResults[textureSetBlockID] = textures;

    return changed;
}

void PatcherMesh::clearTextureSets(const filesystem::path& nifPath)
{
    const unique_lock lock(s_patchedTextureSetsMutex);

    if (s_patchedTextureSets.contains(nifPath)) {
        s_patchedTextureSets.erase(nifPath);
    }
}

PatcherMesh::PatcherMesh(filesystem::path nifPath, nifly::NifFile* nif, string patcherName, const bool& triggerSave)
    : Patcher(std::move(patcherName), triggerSave)
    , m_nifPath(std::move(nifPath))
    , m_nif(nif)
{
}

auto PatcherMesh::getNIFPath() const -> filesystem::path { return m_nifPath; }

auto PatcherMesh::getNIF() const -> nifly::NifFile*
{
    if (m_nif == nullptr) {
        throw std::runtime_error("NIF is null");
    }

    return m_nif;
}

void PatcherMesh::setNIF(nifly::NifFile* nif) { m_nif = nif; }
