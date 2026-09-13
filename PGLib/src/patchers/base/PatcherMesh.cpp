#include "patchers/base/PatcherMesh.hpp"

#include "patchers/base/Patcher.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

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

// Statics.

std::unordered_map<std::filesystem::path, std::unordered_map<uint32_t, PatcherMesh::PatchedTextureSet>>
    PatcherMesh::s_patchedTextureSets;
std::shared_mutex PatcherMesh::s_patchedTextureSetsMutex;

auto PatcherMesh::getTextureSet(const std::filesystem::path& nifPath,
                                nifly::NifFile& nif,
                                nifly::NiShape& nifShape) -> PGTypes::TextureSet
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto texturesetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));

    // Check if in patchedtexturesets.
    const std::shared_lock lock(s_patchedTextureSetsMutex);
    if (s_patchedTextureSets.contains(nifPath) && s_patchedTextureSets.at(nifPath).contains(texturesetBlockID))
        return s_patchedTextureSets.at(nifPath).at(texturesetBlockID).original;

    // Get the texture slots.
    return PGNIFUtil::getTextureSlots(&nif, &nifShape);
}

auto PatcherMesh::setTextureSet(const std::filesystem::path& nifPath,
                                nifly::NifFile& nif,
                                nifly::NiShape& nifShape,
                                const PGTypes::TextureSet& textures) -> bool
{
    auto* const nifShader = nif.GetShader(&nifShape);
    const auto textureSetBlockID = nif.GetBlockID(nif.GetHeader().GetBlock(nifShader->TextureSetRef()));

    bool patchedBefore = false;
    {
        const std::shared_lock lock(s_patchedTextureSetsMutex);
        patchedBefore
            = s_patchedTextureSets.contains(nifPath) && s_patchedTextureSets.at(nifPath).contains(textureSetBlockID);
    }

    if (patchedBefore) {
        // This texture set has been patched before.
        uint32_t newBlockID = 0;

        {
            const std::shared_lock lockAgain(s_patchedTextureSetsMutex);
            const auto& patchResults = s_patchedTextureSets.at(nifPath).at(textureSetBlockID).patchResults;

            // Already been patched, check if it is the same.
            for (const auto& [possibleTexRecordID, possibleTextures] : patchResults) {
                if (possibleTextures == textures) {
                    newBlockID = possibleTexRecordID;

                    if (newBlockID == textureSetBlockID)
                        return false;

                    break;
                }
            }
        }

        // Add a new texture set to the NIF.
        if (newBlockID == 0) {
            auto newTextureSet = std::make_unique<nifly::BSShaderTextureSet>();
            newTextureSet->textures.resize(numTextureSlots);
            for (uint32_t i = 0; i < textures.size(); i++)
                newTextureSet->textures[i] = StringUtil::utf16toASCII(textures.at(i));

            // Set shader reference.
            newBlockID = nif.GetHeader().AddBlock(std::move(newTextureSet));
        }

        auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
        const nifly::NiBlockRef<nifly::BSShaderTextureSet> newBlockRef(newBlockID);
        nifShaderBSLSP->textureSetRef = newBlockRef;

        {
            const std::unique_lock lock(s_patchedTextureSetsMutex);
            s_patchedTextureSets[nifPath][textureSetBlockID].patchResults[newBlockID] = textures;
        }
        return true;
    }

    const std::unique_lock lockWrite(s_patchedTextureSetsMutex);

    // Set original for future use.
    const auto slots = PGNIFUtil::getTextureSlots(&nif, &nifShape);
    s_patchedTextureSets[nifPath][textureSetBlockID].original = slots;

    // Set the texture slots for the shape like normal.
    const bool changed = PGNIFUtil::setTextureSlots(&nif, &nifShape, textures);

    // Update the patchedtexturesets.
    s_patchedTextureSets[nifPath][textureSetBlockID].patchResults[textureSetBlockID] = textures;

    return changed;
}

void PatcherMesh::clearTextureSets(const std::filesystem::path& nifPath)
{
    const std::unique_lock lock(s_patchedTextureSetsMutex);

    if (s_patchedTextureSets.contains(nifPath))
        s_patchedTextureSets.erase(nifPath);
}

PatcherMesh::PatcherMesh(std::filesystem::path nifPath,
                         nifly::NifFile* nif,
                         std::string patcherName)
    : Patcher(std::move(patcherName))
    , m_nifPath(std::move(nifPath))
    , m_nif(nif)
{
}

auto PatcherMesh::getNIFPath() const -> std::filesystem::path { return m_nifPath; }

auto PatcherMesh::getNIF() const -> nifly::NifFile*
{
    if (m_nif == nullptr)
        throw std::runtime_error("NIF is null");

    return m_nif;
}

void PatcherMesh::setNIF(nifly::NifFile* nif) { m_nif = nif; }
