#include "patchers/PatcherMeshPreStockMarker.hpp"

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGMaterialState.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"
#include "util/StringUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

// Keys of the PG_STOCK JSON. Mod authors write these by hand, so they are part of the format and never change.
constexpr const char* keyVersion = "version";
constexpr const char* keyTextureCount = "texture_count";
constexpr const char* keyTextures = "textures";
constexpr const char* keyShader = "shader";

/// @brief Keeps the entries of stock whose value differs from the matching entry of current.
nlohmann::json differingEntries(const nlohmann::json& stock,
                                const nlohmann::json& current)
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [key, stockValue] : stock.items()) {
        const auto it = current.find(key);
        if (it == current.end() || *it != stockValue)
            out[key] = stockValue;
    }

    return out;
}

nifly::BSShaderTextureSet* textureSetOf(nifly::NifFile& nif,
                                        const nifly::BSLightingShaderProperty& shader)
{
    return nif.GetHeader().GetBlock(shader.TextureSetRef());
}

std::wstring shapeLabel(const nifly::NiShape& nifShape) { return StringUtil::utf8toUTF16(nifShape.name.get()); }

} // namespace

auto PatcherMeshPreStockMarker::factory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPre> {
        return std::make_unique<PatcherMeshPreStockMarker>(nifPath, nif);
    };
}

PatcherMeshPreStockMarker::PatcherMeshPreStockMarker(std::filesystem::path nifPath,
                                                     nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath),
                     nif,
                     "StockMarker")
{
}

bool PatcherMeshPreStockMarker::applyPatch(PGTypes::TextureSet& slots,
                                           nifly::NiShape& nifShape)
{
    // Only lighting shader shapes are touched by the shader patchers, so only those have a stock state to keep.
    auto* const shader = dynamic_cast<nifly::BSLightingShaderProperty*>(nif()->GetShader(&nifShape));
    if (!shader)
        return false;

    auto* const marker = findMarker(*nif(), nifShape);
    if (marker) {
        nlohmann::json recorded;
        std::string error;
        if (!parseMarker(*marker, recorded, error)) {
            Logger::warn(L"Ignoring the {} block of shape \"{}\": {}",
                         StringUtil::utf8toUTF16(extraDataName),
                         shapeLabel(nifShape),
                         StringUtil::utf8toUTF16(error));
            return false;
        }

        // The block only lists what a patch changed. Everything else is at its stock value already, so the current
        // state completes the stock state. The complete state goes back into the block until finalizeMarker() trims
        // it again, so the comparison there sees the stock value of every field.
        nlohmann::json stock = captureStock(*nif(), nifShape);
        for (const auto& [key, value] : recorded.items()) {
            if ((key == keyTextures || key == keyShader) && value.is_object() && stock.contains(key)) {
                for (const auto& [subKey, subValue] : value.items())
                    stock[key][subKey] = subValue;
                continue;
            }

            stock[key] = value;
        }

        const bool isChanged = restoreStock(*nif(), nifShape, *shader, stock, slots);
        marker->stringData.get() = stock.dump();

        if (isChanged)
            Logger::trace("Shape reverted to its stock state");

        return isChanged;
    }

    if (!meshUse().isBodySlideShapeData)
        return false;

    // BodySlide clones the extra data of a ShapeData shape into every mesh it builds from it. The block created here
    // lets a later run revert those meshes to stock before patching them. finalizeMarker() keeps only what changed.
    writeMarker(*nif(), nifShape, captureStock(*nif(), nifShape));
    Logger::trace("Added {} block", extraDataName);

    return false;
}

void PatcherMeshPreStockMarker::finalizeMarker(nifly::NifFile& nif,
                                               nifly::NiShape& nifShape)
{
    // applyPatch() leaves the block of a shape without a lighting shader alone, so it is left alone here as well.
    if (!dynamic_cast<nifly::BSLightingShaderProperty*>(nif.GetShader(&nifShape)))
        return;

    auto* const marker = findMarker(nif, nifShape);
    if (!marker)
        return;

    nlohmann::json stock;
    std::string error;
    if (!parseMarker(*marker, stock, error)) {
        // Reported by applyPatch() already. The block is left as it was found.
        return;
    }

    const auto differences = diffStock(nif, nifShape, stock);

    // An empty string marks the block for removeEmptyMarkers().
    marker->stringData.get() = isEmptyStock(differences) ? std::string() : differences.dump();
}

void PatcherMeshPreStockMarker::removeEmptyMarkers(nifly::NifFile& nif)
{
    // Block ID, the shape and the position of the block in the extra data list of the shape.
    std::vector<std::tuple<uint32_t, nifly::NiShape*, uint32_t>> emptyMarkers;
    for (auto* const nifShape : nif.GetShapes()) {
        uint32_t position = 0;
        for (const auto& extraDataRef : nifShape->extraDataRefs) {
            const auto* const block = dynamic_cast<nifly::NiStringExtraData*>(nif.GetHeader().GetBlock(extraDataRef));
            if (block && block->name == extraDataName && block->stringData.get().empty())
                emptyMarkers.emplace_back(extraDataRef.index, nifShape, position);

            position++;
        }
    }

    // Deleting a block renumbers every block behind it, so delete from the back.
    std::ranges::sort(emptyMarkers, [](const auto& a, const auto& b) { return std::get<0>(a) > std::get<0>(b); });
    for (const auto& [blockID, nifShape, position] : emptyMarkers) {
        nif.GetHeader().DeleteBlock(blockID);
        nifShape->extraDataRefs.RemoveBlockRef(position);
    }
}

nlohmann::json PatcherMeshPreStockMarker::captureStock(nifly::NifFile& nif,
                                                       nifly::NiShape& nifShape)
{
    nlohmann::json stock = nlohmann::json::object();
    stock[keyVersion] = formatVersion;

    const auto* const shader = dynamic_cast<nifly::BSLightingShaderProperty*>(nif.GetShader(&nifShape));
    if (!shader)
        return stock;

    stock[keyTextures] = PGMaterialState::textureSlotsToJSON(nif, nifShape);

    const auto* const textureSet = textureSetOf(nif, *shader);
    if (textureSet)
        stock[keyTextureCount] = static_cast<unsigned>(textureSet->textures.size());

    stock[keyShader] = PGMaterialState::shaderToJSON(*shader);

    return stock;
}

nlohmann::json PatcherMeshPreStockMarker::diffStock(nifly::NifFile& nif,
                                                    nifly::NiShape& nifShape,
                                                    const nlohmann::json& stock)
{
    nlohmann::json differences = nlohmann::json::object();
    differences[keyVersion] = formatVersion;

    const auto* const shader = dynamic_cast<nifly::BSLightingShaderProperty*>(nif.GetShader(&nifShape));
    if (!shader)
        return differences;

    const auto texturesIt = stock.find(keyTextures);
    if (texturesIt != stock.end() && texturesIt->is_object()) {
        const auto textures = differingEntries(*texturesIt, PGMaterialState::textureSlotsToJSON(nif, nifShape));
        if (!textures.empty())
            differences[keyTextures] = textures;
    }

    const auto textureCountIt = stock.find(keyTextureCount);
    const auto* const textureSet = textureSetOf(nif, *shader);
    if (textureCountIt != stock.end() && textureCountIt->is_number_unsigned() && textureSet
        && textureCountIt->get<unsigned>() != static_cast<unsigned>(textureSet->textures.size())) {
        differences[keyTextureCount] = *textureCountIt;
    }

    const auto shaderIt = stock.find(keyShader);
    if (shaderIt != stock.end() && shaderIt->is_object()) {
        const auto fields = differingEntries(*shaderIt, PGMaterialState::shaderToJSON(*shader));
        if (!fields.empty())
            differences[keyShader] = fields;
    }

    return differences;
}

bool PatcherMeshPreStockMarker::isEmptyStock(const nlohmann::json& stock)
{
    if (!stock.is_object())
        return true;

    // Only the version may be left.
    return std::ranges::all_of(stock.items(), [](const auto& entry) { return entry.key() == keyVersion; });
}

nifly::NiStringExtraData* PatcherMeshPreStockMarker::findMarker(nifly::NifFile& nif,
                                                                nifly::NiShape& nifShape)
{
    for (const auto& extraDataRef : nifShape.extraDataRefs) {
        auto* const block = dynamic_cast<nifly::NiStringExtraData*>(nif.GetHeader().GetBlock(extraDataRef));
        if (block && block->name == extraDataName)
            return block;
    }

    return nullptr;
}

void PatcherMeshPreStockMarker::writeMarker(nifly::NifFile& nif,
                                            nifly::NiShape& nifShape,
                                            const nlohmann::json& stock)
{
    auto* const marker = findMarker(nif, nifShape);
    if (marker) {
        marker->stringData.get() = stock.dump();
        return;
    }

    auto extraData = std::make_unique<nifly::NiStringExtraData>();
    extraData->name.get() = extraDataName;
    extraData->stringData.get() = stock.dump();
    nif.AssignExtraData(&nifShape, std::move(extraData));
}

size_t PatcherMeshPreStockMarker::markPatchedShapes(nifly::NifFile& stockNif,
                                                    nifly::NifFile& patchedNif,
                                                    const std::wstring& meshLabel)
{
    // A name that appears more than once is matched in order of appearance.
    std::unordered_map<std::string, std::vector<nifly::NiShape*>> patchedShapesByName;
    for (auto* const patchedShape : patchedNif.GetShapes())
        patchedShapesByName[patchedShape->name.get()].push_back(patchedShape);

    std::unordered_set<nifly::NiShape*> matchedShapes;
    size_t numMarked = 0;
    for (auto* const stockShape : stockNif.GetShapes()) {
        nifly::NiShape* patchedShape = nullptr;
        const auto candidatesIt = patchedShapesByName.find(stockShape->name.get());
        if (candidatesIt != patchedShapesByName.end()) {
            for (auto* const candidate : candidatesIt->second) {
                if (!matchedShapes.contains(candidate)) {
                    patchedShape = candidate;
                    break;
                }
            }
        }

        if (!patchedShape) {
            Logger::warn(L"{}: shape \"{}\" only exists in the original mesh, a deleted shape cannot be restored",
                         meshLabel,
                         shapeLabel(*stockShape));
            continue;
        }

        matchedShapes.insert(patchedShape);

        const auto differences = diffStock(patchedNif, *patchedShape, captureStock(stockNif, *stockShape));
        if (isEmptyStock(differences)) {
            // A stale block from an earlier run has nothing to say anymore.
            auto* const staleMarker = findMarker(patchedNif, *patchedShape);
            if (staleMarker)
                staleMarker->stringData.get().clear();

            continue;
        }

        writeMarker(patchedNif, *patchedShape, differences);
        numMarked++;
    }

    for (auto* const patchedShape : patchedNif.GetShapes()) {
        if (!matchedShapes.contains(patchedShape)) {
            Logger::warn(L"{}: shape \"{}\" only exists in the patched mesh, nothing to compare it with",
                         meshLabel,
                         shapeLabel(*patchedShape));
        }
    }

    removeEmptyMarkers(patchedNif);

    return numMarked;
}

bool PatcherMeshPreStockMarker::parseMarker(const nifly::NiStringExtraData& marker,
                                            nlohmann::json& stock,
                                            std::string& error)
{
    const auto& text = marker.stringData.get();
    if (text.empty()) {
        error = "the block is empty";
        return false;
    }

    try {
        stock = nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& e) {
        error = e.what();
        return false;
    }

    if (!stock.is_object()) {
        error = "the block is not a JSON object";
        return false;
    }

    // A block without a version is taken as the current version, which keeps hand written blocks short.
    const auto versionIt = stock.find(keyVersion);
    if (versionIt != stock.end()) {
        if (!versionIt->is_number_unsigned()) {
            error = "the version is not a number";
            return false;
        }

        if (versionIt->get<unsigned>() > formatVersion) {
            error = fmt::format("version {} is newer than this version of PGPatcher supports",
                                versionIt->get<unsigned>());
            return false;
        }
    }

    return true;
}

bool PatcherMeshPreStockMarker::restoreStock(nifly::NifFile& nif,
                                             nifly::NiShape& nifShape,
                                             nifly::BSLightingShaderProperty& shader,
                                             const nlohmann::json& stock,
                                             PGTypes::TextureSet& slots)
{
    bool isChanged = false;

    // Texture set. It is only restored while the slots the patchers see mirror it. They do not when the plugin use has
    // an alternate texture for this shape, which is patched in its own right, or when another shape of the NIF shares
    // the texture set and was patched already, in which case the shared block has to keep that result and the slots
    // of this shape already hold its stock textures.
    auto* const textureSet = textureSetOf(nif, shader);
    if (textureSet && slots == PGNIFUtil::textureSlots(&nif, &nifShape)) {
        // The count comes from mod authored JSON, so it is bounded before it drives an allocation.
        const auto textureCountIt = stock.find(keyTextureCount);
        if (textureCountIt != stock.end() && textureCountIt->is_number_unsigned()
            && textureCountIt->get<uint64_t>() <= numTextureSlots
            && textureCountIt->get<unsigned>() != static_cast<unsigned>(textureSet->textures.size())) {
            textureSet->textures.resize(textureCountIt->get<unsigned>());
            isChanged = true;
        }

        const auto texturesIt = stock.find(keyTextures);
        if (texturesIt != stock.end() && texturesIt->is_object()) {
            for (const auto& [slotKey, texture] : texturesIt->items()) {
                if (!texture.is_string())
                    continue;

                char* end = nullptr;
                const auto slot = std::strtoul(slotKey.c_str(), &end, 10);
                if (!end || *end || slot >= numTextureSlots)
                    continue;

                const auto stockTexture
                    = StringUtil::toLowerASCIIFast(StringUtil::utf8toUTF16(texture.get<std::string>()));
                isChanged |= PGNIFUtil::setTextureSlot(
                    &nif, &nifShape, static_cast<PGEnums::TextureSlots>(slot), stockTexture);
                slots.at(slot) = stockTexture;
            }
        }
    }

    // Shader.
    const auto shaderIt = stock.find(keyShader);
    if (shaderIt != stock.end() && shaderIt->is_object())
        isChanged |= PGMaterialState::shaderFromJSON(shader, *shaderIt);

    // A patch may have removed the vertex colors of the geometry, which the stock flags cannot bring back. Keep the
    // flag in line with the geometry so the engine does not read colors that are not there.
    if (shader.HasVertexColors() && !nifShape.HasVertexColors()) {
        shader.SetVertexColors(false);
        isChanged = true;
    }

    return isChanged;
}
