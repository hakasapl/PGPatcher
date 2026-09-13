#include "pgutil/PGNIFUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
#include "PGRunCache.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

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

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <excpt.h>
#include <filesystem>
#include <istream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

std::map<std::wstring,
         std::tuple<PGEnums::TextureSlots,
                    PGEnums::TextureType>>
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
    const auto& pathStr = pathWithoutExtension.wstring();

    for (const auto& [suffix, slot] : suffixMap) {
        if (boost::iends_with(pathStr, suffix)) {
            // Check if PBR in prefix.
            if (std::get<1>(slot) == PGEnums::TextureType::Height && boost::istarts_with(pathStr, L"textures\\pbr")) {
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
    // Using reinterpret_cast to convert from std::byte to char is more efficient due to less copies
    boost::iostreams::array_source nifArraySource(
        reinterpret_cast<const char*>(nifBytes.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        nifBytes.size());
    boost::iostreams::stream<boost::iostreams::array_source> nifStream(nifArraySource);

    if (!loadNifWithSEH(&nif, &nifStream))
        throw std::runtime_error("Failed to load NIF file");

    if (!nif.IsValid() || !nif.GetHeader().IsValid())
        throw std::runtime_error("NIF did not load properly");

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
    return (nifShaderBSLSP->shaderFlags1 & flag) != 0U;
}

bool PGNIFUtil::hasShaderFlag(const nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag)
{
    return (nifShaderBSLSP->shaderFlags2 & flag) != 0U;
}

bool PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags1& flag)
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 |= flag;
        return true;
    }

    return false;
}

bool PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag)
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
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

// Texture slot helpers.
bool PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const std::wstring& texturePath)
{
    const auto texturePathStr = StringUtil::utf16toASCII(texturePath);
    return setTextureSlot(nif, nifShape, slot, texturePathStr);
}

bool PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const std::string& texturePath)
{
    std::string existingTex;
    nif->GetTextureSlot(nifShape, existingTex, static_cast<unsigned>(slot));
    if (!StringUtil::asciiFastIEquals(existingTex, texturePath)) {
        auto newTex = texturePath;
        nif->SetTextureSlot(nifShape, newTex, static_cast<unsigned>(slot));
        return true;
    }

    return false;
}

bool PGNIFUtil::setTextureSlots(nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                const PGTypes::TextureSet& newSlots)
{
    bool isChanged = false;
    for (uint32_t i = 0; i < numTextureSlots; i++)
        isChanged |= setTextureSlot(nif, nifShape, static_cast<PGEnums::TextureSlots>(i), newSlots.at(i));

    return isChanged;
}

std::string PGNIFUtil::textureSlot(const nifly::NifFile* nif,
                                   nifly::NiShape* nifShape,
                                   const PGEnums::TextureSlots& slot)
{
    std::string texture;
    nif->GetTextureSlot(nifShape, texture, static_cast<unsigned>(slot));
    StringUtil::toLowerASCIIFastInPlace(texture);
    return texture;
}

PGTypes::TextureSet PGNIFUtil::textureSlots(const nifly::NifFile* nif,
                                            nifly::NiShape* nifShape)
{
    PGTypes::TextureSet outSlots;

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
    PGRunCache::recordTexMatch(baseLower, desiredType, outTex);

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
    // All facegen paths start with "meshes\actors\character\facegendata\facegeom\".
    const auto relativePath = path.lexically_relative("meshes/actors/character/facegendata/facegeom");
    return !relativePath.empty() && relativePath.wstring().find(L"..") == std::string::npos;
}
