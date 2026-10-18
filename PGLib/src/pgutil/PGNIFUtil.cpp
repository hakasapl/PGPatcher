#include "pgutil/PGNIFUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
#include "PGRunObserver.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "Animation.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <excpt.h>
#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

const std::map<std::wstring,
               std::tuple<PGEnums::TextureSlots,
                          PGEnums::TextureType>>&
PGNIFUtil::texSuffixMap()
{
    static const std::map<std::wstring, std::tuple<PGEnums::TextureSlots, PGEnums::TextureType>> textureSuffixMap = {
        { L"_bl", { PGEnums::TextureSlots::Backlight, PGEnums::TextureType::Backlight } },
        { L"_b", { PGEnums::TextureSlots::Backlight, PGEnums::TextureType::Backlight } },
        { L"_flow", { PGEnums::TextureSlots::Backlight, PGEnums::TextureType::HairFlowMap } },
        { L"_cnr", { PGEnums::TextureSlots::MultiLayer, PGEnums::TextureType::CoatNormalRoughness } },
        { L"_s", { PGEnums::TextureSlots::MultiLayer, PGEnums::TextureType::SubsurfaceTint } },
        { L"_i", { PGEnums::TextureSlots::MultiLayer, PGEnums::TextureType::InnerLayer } },
        { L"_f", { PGEnums::TextureSlots::MultiLayer, PGEnums::TextureType::FuzzPBR } },
        { L"_rmaos", { PGEnums::TextureSlots::EnvMask, PGEnums::TextureType::RMAOS } },
        { L"_envmask", { PGEnums::TextureSlots::EnvMask, PGEnums::TextureType::EnvironmentMask } },
        { L"_em", { PGEnums::TextureSlots::EnvMask, PGEnums::TextureType::EnvironmentMask } },
        { L"_m", { PGEnums::TextureSlots::EnvMask, PGEnums::TextureType::EnvironmentMask } },
        { L"_e", { PGEnums::TextureSlots::Cubemap, PGEnums::TextureType::Cubemap } },
        { L"_p", { PGEnums::TextureSlots::Parallax, PGEnums::TextureType::Height } },
        { L"_sk", { PGEnums::TextureSlots::Glow, PGEnums::TextureType::SkinTint } },
        { L"_g", { PGEnums::TextureSlots::Glow, PGEnums::TextureType::Emissive } },
        { L"_msn", { PGEnums::TextureSlots::Normal, PGEnums::TextureType::Normal } },
        { L"_n", { PGEnums::TextureSlots::Normal, PGEnums::TextureType::Normal } },
        { L"_d", { PGEnums::TextureSlots::Diffuse, PGEnums::TextureType::Diffuse } },
        { L"mask", { PGEnums::TextureSlots::Diffuse, PGEnums::TextureType::Diffuse } },
    };

    return textureSuffixMap;
}

PGEnums::TextureSlots PGNIFUtil::slotFromTexType(const PGEnums::TextureType& type)
{
    static std::unordered_map<PGEnums::TextureType, PGEnums::TextureSlots> texTypeToSlotMap = {
        { PGEnums::TextureType::Diffuse, PGEnums::TextureSlots::Diffuse },
        { PGEnums::TextureType::Normal, PGEnums::TextureSlots::Normal },
        { PGEnums::TextureType::ModelSpaceNormal, PGEnums::TextureSlots::Normal },
        { PGEnums::TextureType::Emissive, PGEnums::TextureSlots::Glow },
        { PGEnums::TextureType::SkinTint, PGEnums::TextureSlots::Glow },
        { PGEnums::TextureType::SubsurfaceColor, PGEnums::TextureSlots::Glow },
        { PGEnums::TextureType::Height, PGEnums::TextureSlots::Parallax },
        { PGEnums::TextureType::HeightPBR, PGEnums::TextureSlots::Parallax },
        { PGEnums::TextureType::Cubemap, PGEnums::TextureSlots::Cubemap },
        { PGEnums::TextureType::EnvironmentMask, PGEnums::TextureSlots::EnvMask },
        { PGEnums::TextureType::ComplexMaterial, PGEnums::TextureSlots::EnvMask },
        { PGEnums::TextureType::RMAOS, PGEnums::TextureSlots::EnvMask },
        { PGEnums::TextureType::SubsurfaceTint, PGEnums::TextureSlots::MultiLayer },
        { PGEnums::TextureType::InnerLayer, PGEnums::TextureSlots::MultiLayer },
        { PGEnums::TextureType::FuzzPBR, PGEnums::TextureSlots::MultiLayer },
        { PGEnums::TextureType::CoatNormalRoughness, PGEnums::TextureSlots::MultiLayer },
        { PGEnums::TextureType::Backlight, PGEnums::TextureSlots::Backlight },
        { PGEnums::TextureType::Specular, PGEnums::TextureSlots::Backlight },
        { PGEnums::TextureType::HairFlowMap, PGEnums::TextureSlots::Backlight },
        { PGEnums::TextureType::SubsurfacePBR, PGEnums::TextureSlots::Backlight },
        { PGEnums::TextureType::Unknown, PGEnums::TextureSlots::Unknown },
    };

    if (texTypeToSlotMap.contains(type))
        return texTypeToSlotMap[type];

    return texTypeToSlotMap[PGEnums::TextureType::Unknown];
}

std::tuple<PGEnums::TextureSlots,
           PGEnums::TextureType>
PGNIFUtil::defaultsFromSuffix(const std::filesystem::path& path)
{
    const auto& suffixMap = texSuffixMap();

    // Get the texture suffix.
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    auto pathStr = pathWithoutExtension.wstring();
    // Faster ascii lower is okay here because ALL textures must be purely ascii by the time they reach here.
    StringUtil::toLowerASCIIFastInPlace(pathStr);

    for (const auto& [suffix, slot] : suffixMap) {
        if (pathStr.ends_with(suffix)) {
            // Check if PBR in prefix.
            if (std::get<1>(slot) == PGEnums::TextureType::Height && pathStr.starts_with(L"textures\\pbr")) {
                // This is a PBR heightmap so it gets a different texture type.
                return { PGEnums::TextureSlots::Parallax, PGEnums::TextureType::HeightPBR };
            }

            return slot;
        }
    }

    // Default return diffuse.
    return { PGEnums::TextureSlots::Unknown, PGEnums::TextureType::Unknown };
}

extern "C" bool loadNifWithSEH(nifly::NifFile* pNif,
                               std::istream* pStream)
{
    __try {
        pNif->Load(*pStream);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Catch the access violation or other SEH exception.
        return false;
    }
}

nifly::NifFile PGNIFUtil::loadNIFFromBytes(const std::vector<std::byte>& nifBytes,
                                           const bool& shouldRunChecks)
{
    // NIF file object.
    nifly::NifFile nif;

    // Get NIF Bytes.
    if (nifBytes.empty())
        throw std::runtime_error("File is empty");

    // Convert Byte Vector to Stream.
    // Using reinterpret_cast to convert from std::byte to char is more efficient due to less copies.
    boost::iostreams::array_source nifArraySource(
        reinterpret_cast<const char*>(nifBytes.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        nifBytes.size());
    boost::iostreams::stream<boost::iostreams::array_source> nifStream(nifArraySource);

    if (!loadNifWithSEH(&nif, &nifStream))
        throw std::runtime_error("Failed to load NIF file");

    if (!nif.IsValid() || !nif.GetHeader().IsValid())
        throw std::runtime_error("NIF did not load properly");

    // nifly reads a truncated file without complaint, filling the missing blocks with whatever the short reads left.
    if (nifStream.fail())
        throw std::runtime_error("NIF is truncated");

    if (!shouldRunChecks)
        return nif;

    // Check shapes.
    const auto shapes = nif.GetShapes();
    for (const auto& shape : shapes) {
        if (!shape)
            throw std::runtime_error("NIF contains a null shape");

        auto* nifShader = nif.GetShader(shape);
        if (nifShader && nifShader->HasTextureSet()) {
            const auto* txstRec = nif.GetHeader().GetBlock(nifShader->TextureSetRef());
            if (!txstRec)
                throw std::runtime_error("NIF contains reference to texture set that does not exist");
        }

        // Check for any non-ascii chars.
        for (uint32_t slot = 0; slot < numTextureSlots; slot++) {
            std::string texture;
            nif.GetTextureSlot(shape, texture, slot);

            if (!StringUtil::containsOnlyAscii(texture)) {
                // NIFs cannot have non-ascii chars in their texture slots.
                throw std::runtime_error("NIF contains non-ascii characters in texture slot(s)");
            }
        }
    }

    return nif;
}

bool PGNIFUtil::setShaderType(nifly::NiShader* nifShader,
                              const nifly::BSLightingShaderPropertyShaderType& type)
{
    if (!nifShader)
        return false;

    if (nifShader->GetShaderType() != type) {
        nifShader->SetShaderType(type);
        return true;
    }

    return false;
}

bool PGNIFUtil::setShaderFloat(float& value,
                               const float& newValue)
{
    if (fabs(value - newValue) > minFloatComparison) {
        value = newValue;
        return true;
    }

    return false;
}

bool PGNIFUtil::setShaderVec2(nifly::Vector2& value,
                              const nifly::Vector2& newValue)
{
    if (value != newValue) {
        value = newValue;
        return true;
    }

    return false;
}

// Shader flag helpers.
bool PGNIFUtil::hasShaderFlag(const nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags1& flag)
{
    if (!nifShaderBSLSP)
        return false;

    return (nifShaderBSLSP->shaderFlags1 & flag) != 0U;
}

bool PGNIFUtil::hasShaderFlag(const nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag)
{
    if (!nifShaderBSLSP)
        return false;

    return (nifShaderBSLSP->shaderFlags2 & flag) != 0U;
}

bool PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags1& flag)
{
    if (nifShaderBSLSP && !hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 |= flag;
        return true;
    }

    return false;
}

bool PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag)
{
    if (nifShaderBSLSP && !hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 |= flag;
        return true;
    }

    return false;
}

bool PGNIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                const nifly::SkyrimShaderPropertyFlags1& flag)
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 &= ~flag;
        return true;
    }

    return false;
}

bool PGNIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                const nifly::SkyrimShaderPropertyFlags2& flag)
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 &= ~flag;
        return true;
    }

    return false;
}

bool PGNIFUtil::configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                    const nifly::SkyrimShaderPropertyFlags1& flag,
                                    const bool& enable)
{
    bool isChanged = false;
    if (enable)
        isChanged |= setShaderFlag(nifShaderBSLSP, flag);
    else
        isChanged |= clearShaderFlag(nifShaderBSLSP, flag);

    return isChanged;
}

bool PGNIFUtil::configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                    const nifly::SkyrimShaderPropertyFlags2& flag,
                                    const bool& enable)
{
    bool isChanged = false;
    if (enable)
        isChanged |= setShaderFlag(nifShaderBSLSP, flag);
    else
        isChanged |= clearShaderFlag(nifShaderBSLSP, flag);

    return isChanged;
}

// Shared block helpers, see the note on shared blocks in PGNIFUtil.hpp. Shaders, alpha properties and texture sets are
// copied through unshareBlock(), and an alpha property is deleted through deleteBlockTree().
namespace {

/// @brief Checks whether more than one reference points at a block.
bool isSharedBlock(const nifly::NiHeader& header,
                   uint32_t blockID)
{
    // Child indices, unlike child references, are collected without an allocation per block.
    std::vector<uint32_t> childIDs;
    size_t refCount = 0;
    for (uint32_t curBlockID = 0; curBlockID < header.GetNumBlocks(); curBlockID++) {
        auto* const block = header.GetBlock<nifly::NiObject>(curBlockID);
        if (!block)
            continue;

        childIDs.clear();
        block->GetChildIndices(childIDs);
        refCount += static_cast<size_t>(std::ranges::count(childIDs, blockID));
        if (refCount > 1)
            return true;
    }

    return false;
}

/// @brief Gets the block a reference points to, ready to be changed for the block that holds the reference alone
///
/// A block that other blocks also reference is copied and the reference is pointed at the copy, which is returned. The
/// other blocks keep the original. A block only this reference uses is returned as it is, to be changed in place.
template<typename T>
T* unshareBlock(nifly::NiHeader& header,
                nifly::NiBlockRef<T>& ref)
{
    auto* const block = header.GetBlock(ref);
    if (!block || !isSharedBlock(header, ref.index))
        return block;

    auto blockCopy = block->Clone();
    auto* const copy = blockCopy.get();
    ref.index = header.AddBlock(std::move(blockCopy));
    return copy;
}

/// @brief Gets the texture set of a shape, ready to be changed for that shape alone (see unshareBlock())
nifly::BSShaderTextureSet* unshareTextureSet(nifly::NifFile& nif,
                                             nifly::NiShape& nifShape)
{
    auto* const shader = nif.GetShader(&nifShape);
    if (!shader || !shader->TextureSetRef())
        return nullptr;

    return unshareBlock(nif.GetHeader(), *shader->TextureSetRef());
}

/// @brief Adds blocks, and every block they reach through their references, to a set
///
/// Scene graph blocks (nodes and shapes) are never reached: a property tree does not contain them, so a reference to
/// one is a broken file. Pointers, such as the target of a controller, are followed only into the given blocks.
void addReachableBlocks(const nifly::NiHeader& header,
                        std::vector<nifly::NiObject*> pending,
                        std::unordered_set<nifly::NiObject*>& reached,
                        const std::unordered_set<nifly::NiObject*>* ptrTargets = nullptr)
{
    while (!pending.empty()) {
        auto* const block = pending.back();
        pending.pop_back();
        if (!reached.insert(block).second)
            continue;

        std::set<nifly::NiRef*> refs;
        block->GetChildRefs(refs);
        for (const auto* const ref : refs) {
            auto* const child = header.GetBlock<nifly::NiObject>(ref);
            if (child && !dynamic_cast<nifly::NiAVObject*>(child))
                pending.push_back(child);
        }

        if (!ptrTargets)
            continue;

        std::set<nifly::NiRef*> ptrs;
        block->GetPtrs(ptrs);
        for (const auto* const ptr : ptrs) {
            auto* const target = header.GetBlock<nifly::NiObject>(ptr);
            if (target && ptrTargets->contains(target))
                pending.push_back(target);
        }
    }
}

/// @brief Deletes a block together with every block only it uses
///
/// A block it reaches that a block outside of its reach references stays for that block, with everything that one
/// reaches or points at. References to the root block itself do not count: the root goes unless a kept block points
/// at it. The links of an animation sequence follow the controllers they drive instead of keeping them: a link to a
/// deleted controller is removed, together with what only that link used, while a link whose controller stays keeps
/// its interpolator.
void deleteBlockTree(nifly::NifFile& nif,
                     nifly::NiObject& root)
{
    auto& header = nif.GetHeader();

    std::unordered_set<nifly::NiObject*> treeBlocks;
    addReachableBlocks(header, { &root }, treeBlocks);

    std::vector<nifly::NiObject*> sharedBlocks;
    for (uint32_t blockID = 0; blockID < header.GetNumBlocks(); blockID++) {
        auto* const block = header.GetBlock<nifly::NiObject>(blockID);
        if (!block || treeBlocks.contains(block))
            continue;

        std::set<nifly::NiRef*> refs;
        auto* const sequence = dynamic_cast<nifly::NiSequence*>(block);
        if (sequence) {
            for (auto& link : sequence->controlledBlocks)
                if (!treeBlocks.contains(header.GetBlock<nifly::NiObject>(link.controllerRef)))
                    refs.insert(&link.interpolatorRef);
        } else {
            block->GetChildRefs(refs);
        }

        for (const auto* const ref : refs) {
            auto* const child = header.GetBlock<nifly::NiObject>(ref);
            if (child && child != &root && treeBlocks.contains(child))
                sharedBlocks.push_back(child);
        }
    }

    std::unordered_set<nifly::NiObject*> keptBlocks;
    addReachableBlocks(header, sharedBlocks, keptBlocks, &treeBlocks);

    std::unordered_set<uint32_t> deletedBlockIDs;
    for (auto* const block : treeBlocks)
        if (!keptBlocks.contains(block))
            deletedBlockIDs.insert(nif.GetBlockID(block));

    // The links of an animation sequence to deleted blocks go with them. Their interpolators are deleted below if the
    // links were their only users.
    std::unordered_set<nifly::NiObject*> linkInterpolators;
    for (uint32_t blockID = 0; blockID < header.GetNumBlocks(); blockID++) {
        auto* const sequence = header.GetBlock<nifly::NiSequence>(blockID);
        if (!sequence)
            continue;

        auto& links = sequence->controlledBlocks;
        for (uint32_t i = links.size(); i > 0; i--) {
            const auto& link = links[i - 1];
            if (!deletedBlockIDs.contains(link.controllerRef.index)
                && !deletedBlockIDs.contains(link.interpolatorRef.index)) {
                continue;
            }

            auto* const interpolator = header.GetBlock<nifly::NiObject>(link.interpolatorRef);
            if (interpolator && !deletedBlockIDs.contains(link.interpolatorRef.index))
                linkInterpolators.insert(interpolator);

            links.erase(i - 1);
        }
    }

    // Deleting a block renumbers the blocks behind it, so delete from the back. The other blocks stay where they are.
    std::vector<uint32_t> sortedBlockIDs(deletedBlockIDs.begin(), deletedBlockIDs.end());
    std::ranges::sort(sortedBlockIDs, std::greater<>());
    for (const auto blockID : sortedBlockIDs)
        header.DeleteBlock(blockID);

    for (auto* const interpolator : linkInterpolators) {
        const auto interpolatorID = nif.GetBlockID(interpolator);
        if (interpolatorID != nifly::NIF_NPOS && !header.IsBlockReferenced(interpolatorID, false))
            deleteBlockTree(nif, *interpolator);
    }
}

} // namespace

// Shader helpers.
bool PGNIFUtil::unshareShader(nifly::NifFile& nif,
                              nifly::NiShape& nifShape)
{
    auto* const shaderRef = nifShape.ShaderPropertyRef();
    if (!shaderRef)
        return false;

    // The shape patchers write lighting shaders only, so no other shader type needs a copy. A shader with a controller
    // stays shared, like an alpha property with one: the controller animates that one block.
    auto& header = nif.GetHeader();
    const auto* const shader = header.GetBlock<nifly::BSLightingShaderProperty>(shaderRef->index);
    if (!shader || header.GetBlock(shader->controllerRef))
        return false;

    return unshareBlock(header, *shaderRef) != shader;
}

// Alpha property helpers.
bool PGNIFUtil::setAlphaProperty(nifly::NifFile& nif,
                                 nifly::NiShape& nifShape,
                                 const std::optional<uint16_t>& flags,
                                 const std::optional<uint8_t>& threshold,
                                 bool shouldCreate)
{
    // Shape types without an alpha property reference cannot get one.
    auto* const alphaPropertyRef = nifShape.AlphaPropertyRef();
    if (!alphaPropertyRef)
        return false;

    auto& header = nif.GetHeader();
    auto* alphaProperty = header.GetBlock(*alphaPropertyRef);
    if (alphaProperty) {
        if ((!flags || alphaProperty->flags == *flags) && (!threshold || alphaProperty->threshold == *threshold))
            return false;

        // A controller animates the one alpha property it targets, so a property with one stays shared and changes for
        // every shape that uses it, which keeps all of them animated.
        if (!header.GetBlock(alphaProperty->controllerRef))
            alphaProperty = unshareBlock(header, *alphaPropertyRef);
    } else {
        if (!shouldCreate)
            return false;

        auto newAlphaProperty = std::make_unique<nifly::NiAlphaProperty>();
        alphaProperty = newAlphaProperty.get();
        alphaPropertyRef->index = header.AddBlock(std::move(newAlphaProperty));
    }

    if (flags)
        alphaProperty->flags = *flags;

    if (threshold)
        alphaProperty->threshold = *threshold;

    return true;
}

bool PGNIFUtil::deleteAlphaProperty(nifly::NifFile& nif,
                                    nifly::NiShape& nifShape)
{
    auto* const alphaPropertyRef = nifShape.AlphaPropertyRef();
    if (!alphaPropertyRef)
        return false;

    auto& header = nif.GetHeader();
    auto* const alphaProperty = header.GetBlock(*alphaPropertyRef);
    if (!alphaProperty)
        return false;

    alphaPropertyRef->Clear();
    if (!header.IsBlockReferenced(nif.GetBlockID(alphaProperty), false))
        deleteBlockTree(nif, *alphaProperty);

    return true;
}

// Texture slot helpers.
bool PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const std::wstring& texturePath)
{
    if (!nif || !nifShape || static_cast<size_t>(slot) >= numTextureSlots)
        return false;

    // A single slot is written like every slot, so both follow the same rules.
    auto newSlots = textureSlots(nif, nifShape);
    newSlots.at(static_cast<size_t>(slot)) = texturePath;
    return setTextureSlots(nif, nifShape, newSlots);
}

bool PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const std::string& texturePath)
{
    return setTextureSlot(nif, nifShape, slot, StringUtil::asciitoUTF16(texturePath));
}

bool PGNIFUtil::setTextureSlots(nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                const PGTypes::TextureSet& newSlots)
{
    if (!nif || !nifShape || !nif->GetShader(nifShape))
        return false;

    // Making the texture set the shape's own takes a pass over every block, so it happens once for all changed slots.
    std::vector<std::pair<unsigned, std::string>> changedSlots;
    for (unsigned slot = 0; slot < numTextureSlots; slot++) {
        std::string existingTex;
        nif->GetTextureSlot(nifShape, existingTex, slot);
        auto newTex = StringUtil::utf16toASCII(newSlots.at(slot));
        if (!StringUtil::asciiFastIEquals(existingTex, newTex))
            changedSlots.emplace_back(slot, std::move(newTex));
    }

    if (changedSlots.empty())
        return false;

    // An effect shader has no texture set to copy; its source texture is written below like before.
    unshareTextureSet(*nif, *nifShape);
    for (auto& [slot, texture] : changedSlots)
        nif->SetTextureSlot(nifShape, texture, slot);

    return true;
}

std::string PGNIFUtil::textureSlot(const nifly::NifFile* nif,
                                   nifly::NiShape* nifShape,
                                   const PGEnums::TextureSlots& slot)
{
    if (!nif || !nifShape)
        return { };

    std::string texture;
    nif->GetTextureSlot(nifShape, texture, static_cast<unsigned>(slot));
    StringUtil::toLowerASCIIFastInPlace(texture);
    return texture;
}

PGTypes::TextureSet PGNIFUtil::textureSlots(const nifly::NifFile* nif,
                                            nifly::NiShape* nifShape)
{
    PGTypes::TextureSet outSlots;
    if (!nif || !nifShape)
        return outSlots;

    for (uint32_t i = 0; i < numTextureSlots; i++) {
        std::string texture;
        const uint32_t result = nif->GetTextureSlot(nifShape, texture, i);
        StringUtil::toLowerASCIIFastInPlace(texture);

        if (!result || texture.empty()) {
            // No texture in Slot.
            continue;
        }

        outSlots.at(i) = StringUtil::asciitoUTF16(texture);
    }

    return outSlots;
}

PGTypes::TextureSetStr PGNIFUtil::textureSetToStr(const PGTypes::TextureSet& set)
{
    PGTypes::TextureSetStr outSet;

    for (uint32_t i = 0; i < numTextureSlots; i++)
        outSet.at(i) = StringUtil::utf16toUTF8(set.at(i));

    return outSet;
}

std::wstring PGNIFUtil::texBase(const std::filesystem::path& path,
                                const PGEnums::TextureSlots& slot)
{
    const auto& suffixMap = texSuffixMap();

    // Get the texture suffix.
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    auto pathStr = pathWithoutExtension.wstring();
    // Faster ascii lower is okay here because ALL textures must be purely ascii by the time they reach here.
    StringUtil::toLowerASCIIFastInPlace(pathStr);

    if (slot == PGEnums::TextureSlots::Unknown) {
        // Just return path without extension.
        return pathStr;
    }

    for (const auto& [suffix, curSlot] : suffixMap) {
        if (slot != std::get<0>(curSlot))
            continue;

        if (pathStr.ends_with(suffix))
            return pathStr.substr(0, pathStr.size() - suffix.size());
    }

    return pathStr;
}

std::vector<PGTypes::PGTexture>
PGNIFUtil::texMatch(const std::wstring& base,
                    const PGEnums::TextureType& desiredType,
                    const std::map<std::wstring,
                                   std::unordered_set<PGTypes::PGTexture,
                                                      PGTypes::PGTextureHasher>>& searchMap)
{
    // Binary search on base list.
    const std::wstring baseLower = StringUtil::toLowerASCIIFast(base);
    const auto it = searchMap.find(baseLower);

    std::vector<PGTypes::PGTexture> outTex;
    if (it != searchMap.end() && boost::equals(it->first, baseLower)) {
        for (const auto& texture : it->second)
            if (texture.type == desiredType)
                outTex.push_back(texture);
    }

    // Record lookup for incremental runs (no-op unless a mesh is being recorded on this thread).
    PGRunObserver::current().recordTexMatch(baseLower, desiredType, outTex);

    return outTex;
}

PGTypes::TextureSet PGNIFUtil::searchPrefixes(nifly::NifFile const& nif,
                                              nifly::NiShape* nifShape,
                                              const bool& shouldFindBaseSlots)
{
    PGTypes::TextureSet outPrefixes;

    // Loop through each texture Slot.
    for (uint32_t i = 0; i < numTextureSlots; i++) {
        std::string texture;
        const uint32_t result = nif.GetTextureSlot(nifShape, texture, i);

        if (!result || texture.empty()) {
            // No texture in Slot.
            continue;
        }

        // Get default suffixes.
        std::wstring texBase;
        if (shouldFindBaseSlots) {
            // Get the base texture name without suffix.
            texBase = PGNIFUtil::texBase(StringUtil::asciitoUTF16(texture), static_cast<PGEnums::TextureSlots>(i));
        } else {
            // Get the full texture name.
            texBase = PGNIFUtil::texBase(StringUtil::asciitoUTF16(texture));
        }

        outPrefixes.at(i) = texBase;
    }

    return outPrefixes;
}

PGTypes::TextureSet PGNIFUtil::searchPrefixes(const PGTypes::TextureSet& oldSlots,
                                              const bool& shouldFindBaseSlots)
{
    PGTypes::TextureSet outSlots;

    for (uint32_t i = 0; i < numTextureSlots; i++) {
        if (oldSlots.at(i).empty())
            continue;

        std::wstring texBase;
        if (shouldFindBaseSlots) {
            // Get the base texture name without suffix.
            texBase = PGNIFUtil::texBase(oldSlots.at(i), static_cast<PGEnums::TextureSlots>(i));
        } else {
            // Get the full texture name.
            texBase = PGNIFUtil::texBase(oldSlots.at(i));
        }
        outSlots.at(i) = texBase;
    }

    return outSlots;
}

std::vector<std::pair<nifly::NiShape*,
                      int>>
PGNIFUtil::shapesWith3DIdx(const nifly::NifFile* nif)
{
    if (!nif)
        throw std::runtime_error("NIF is null");

    std::vector<nifly::NiObject*> tree;
    nif->GetTree(tree);
    std::vector<std::pair<nifly::NiShape*, int>> shapes;
    int oldIndex3D = 0;
    for (auto& obj : tree) {
        auto* const curShape = dynamic_cast<nifly::NiShape*>(obj);
        if (curShape) {
            shapes.emplace_back(curShape, oldIndex3D++);
            continue;
        }

        // Other stuff that should increment oldIndex3D.
        if (dynamic_cast<nifly::NiParticleSystem*>(obj)) {
            // Particle system, increment index3d.
            oldIndex3D++;
        }
    }

    return shapes;
}

bool PGNIFUtil::hasVertexPositions(nifly::NiShape* nifShape)
{
    return nifShape != nullptr && nifShape->HasVertices()
        && dynamic_cast<nifly::BSDynamicTriShape*>(nifShape) == nullptr;
}

bool PGNIFUtil::isPatchableShape(const nifly::NifFile& nif,
                                 nifly::NiShape& nifShape)
{
    const std::string shapeBlockName = nifShape.GetBlockName();

    // Check if shape should be patched or not.
    if (shapeBlockName != "NiTriShape" && shapeBlockName != "BSTriShape" && shapeBlockName != "BSLODTriShape"
        && shapeBlockName != "BSMeshLODTriShape" && shapeBlockName != "BSDynamicTriShape") {
        return false;
    }

    // Get NIFShader type.
    if (!nifShape.HasShaderProperty())
        return false;

    // Get NIFShader from shape.
    const nifly::NiShader* nifShader = nif.GetShader(&nifShape);
    return nifShader != nullptr;
}

bool PGNIFUtil::isShaderPatchableShape(nifly::NifFile& nif,
                                       nifly::NiShape& nifShape)
{
    const std::string shapeBlockName = nifShape.GetBlockName();

    if (!isPatchableShape(nif, nifShape))
        return false;

    // Check that NIFShader is a BSLightingShaderProperty.
    nifly::NiShader* nifShader = nif.GetShader(&nifShape);
    const std::string nifShaderName = nifShader->GetBlockName();
    if (nifShaderName != "BSLightingShaderProperty")
        return false;

    // Check that NIFShader has a texture set.
    if (!nifShader->HasTextureSet())
        return false;

    // Check if PG_IGNORE is set on shader or shape.
    const auto checkIgnoreFlag = [&nif](auto& extraDataRefs) {
        for (const auto& extraDataRef : extraDataRefs) {
            auto* const curBlock = nif.GetHeader().GetBlock(extraDataRef);
            const auto* const booleanBlock = dynamic_cast<nifly::NiBooleanExtraData*>(curBlock);
            if (booleanBlock && booleanBlock->name == "PG_IGNORE" && booleanBlock->booleanData)
                return true; // PG_IGNORE found and set to true
        }
        return false;
    };

    return !checkIgnoreFlag(nifShader->extraDataRefs) && !checkIgnoreFlag(nifShape.extraDataRefs);
}

bool PGNIFUtil::isFacegenMesh(const std::filesystem::path& path)
{
    // All facegen paths start with the facegen folder.
    const std::filesystem::path lowerPath = StringUtil::toLowerASCIIFast(path.wstring());
    const auto relativePath = lowerPath.lexically_relative(facegenFolder);
    return !relativePath.empty() && relativePath.wstring().find(L"..") == std::string::npos;
}

bool PGNIFUtil::isBodySlideShapeDataMesh(const std::filesystem::path& path)
{
    // BodySlide reads the meshes of its projects from "CalienteTools\BodySlide\ShapeData\".
    const std::filesystem::path lowerPath = StringUtil::toLowerASCIIFast(path.wstring());
    const auto relativePath = lowerPath.lexically_relative("calientetools/bodyslide/shapedata");
    return !relativePath.empty() && relativePath.wstring().find(L"..") == std::string::npos;
}
