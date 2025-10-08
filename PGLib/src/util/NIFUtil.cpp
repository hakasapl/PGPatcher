#include "util/NIFUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
#include "util/ParallaxGenUtil.hpp"

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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

using namespace std;

auto NIFUtil::getStrFromShader(const ShapeShader& shader) -> string
{
    const static unordered_map<NIFUtil::ShapeShader, string> strFromShaderMap
        = { { ShapeShader::NONE, "Default" }, { ShapeShader::UNKNOWN, "Unknown" }, { ShapeShader::TRUEPBR, "PBR" },
              { ShapeShader::COMPLEXMATERIAL, "Complex Material" }, { ShapeShader::VANILLAPARALLAX, "Parallax" } };

    if (strFromShaderMap.find(shader) != strFromShaderMap.end()) {
        return strFromShaderMap.at(shader);
    }

    return strFromShaderMap.at(ShapeShader::UNKNOWN);
}

auto NIFUtil::getShaderFromStr(const string& shader) -> ShapeShader
{
    const static unordered_map<string, ShapeShader> shaderFromStrMap
        = { { "Default", ShapeShader::NONE }, { "Unknown", ShapeShader::UNKNOWN }, { "PBR", ShapeShader::TRUEPBR },
              { "Complex Material", ShapeShader::COMPLEXMATERIAL }, { "Parallax", ShapeShader::VANILLAPARALLAX } };

    const auto searchKey = boost::to_lower_copy(shader);
    if (shaderFromStrMap.find(searchKey) != shaderFromStrMap.end()) {
        return shaderFromStrMap.at(searchKey);
    }

    return ShapeShader::UNKNOWN;
}

auto NIFUtil::getTextureSlotsFromStr(const string& slots) -> TextureSet
{
    TextureSet textureSlots;
    vector<string> splitSlots;
    boost::split(splitSlots, slots, boost::is_any_of(","));
    for (size_t i = 0; i < splitSlots.size(); ++i) {
        if (i < NUM_TEXTURE_SLOTS) {
            textureSlots.at(i) = ParallaxGenUtil::utf8toUTF16(splitSlots[i]);
        }
    }
    return textureSlots;
}

auto NIFUtil::getStrFromTextureSlots(const TextureSet& slots) -> string
{
    string strSlots;
    for (const auto& slot : slots) {
        if (!strSlots.empty()) {
            strSlots += ",";
        }
        strSlots += ParallaxGenUtil::utf16toUTF8(slot);
    }
    return strSlots;
}

auto NIFUtil::getStrFromTexAttribute(const TextureAttribute& attribute) -> std::string
{
    static unordered_map<TextureAttribute, string> strFromTexAttributeMap
        = { { TextureAttribute::CM_ENVMASK, "CM_ENVMASK" }, { TextureAttribute::CM_GLOSSINESS, "CM_GLOSSINESS" },
              { TextureAttribute::CM_METALNESS, "CM_METALNESS" }, { TextureAttribute::CM_HEIGHT, "CM_HEIGHT" } };

    if (strFromTexAttributeMap.find(attribute) != strFromTexAttributeMap.end()) {
        return strFromTexAttributeMap[attribute];
    }

    return "";
}

auto NIFUtil::getStrSetFromTexAttributeSet(const unordered_set<TextureAttribute>& attributeSet) -> unordered_set<string>
{
    unordered_set<string> strSet;
    for (const auto& attr : attributeSet) {
        strSet.insert(getStrFromTexAttribute(attr));
    }

    return strSet;
}

auto NIFUtil::getDefaultTextureType(const TextureSlots& slot) -> TextureType
{
    switch (slot) {
    case TextureSlots::DIFFUSE:
        return TextureType::DIFFUSE;
    case TextureSlots::NORMAL:
        return TextureType::NORMAL;
    case TextureSlots::GLOW:
        return TextureType::EMISSIVE;
    case TextureSlots::PARALLAX:
        return TextureType::HEIGHT;
    case TextureSlots::CUBEMAP:
        return TextureType::CUBEMAP;
    case TextureSlots::ENVMASK:
        return TextureType::ENVIRONMENTMASK;
    case TextureSlots::MULTILAYER:
        return TextureType::SUBSURFACETINT;
    case TextureSlots::BACKLIGHT:
        return TextureType::BACKLIGHT;
    default:
        return TextureType::DIFFUSE;
    }
}

auto NIFUtil::getTexSuffixMap() -> map<wstring, tuple<NIFUtil::TextureSlots, NIFUtil::TextureType>>
{
    static const map<wstring, tuple<NIFUtil::TextureSlots, NIFUtil::TextureType>> textureSuffixMap
        = { { L"_bl", { NIFUtil::TextureSlots::BACKLIGHT, NIFUtil::TextureType::BACKLIGHT } },
              { L"_b", { NIFUtil::TextureSlots::BACKLIGHT, NIFUtil::TextureType::BACKLIGHT } },
              { L"_flow", { NIFUtil::TextureSlots::BACKLIGHT, NIFUtil::TextureType::HAIR_FLOWMAP } },
              { L"_cnr", { NIFUtil::TextureSlots::MULTILAYER, NIFUtil::TextureType::COATNORMALROUGHNESS } },
              { L"_s", { NIFUtil::TextureSlots::MULTILAYER, NIFUtil::TextureType::SUBSURFACETINT } },
              { L"_i", { NIFUtil::TextureSlots::MULTILAYER, NIFUtil::TextureType::INNERLAYER } },
              { L"_f", { NIFUtil::TextureSlots::MULTILAYER, NIFUtil::TextureType::FUZZPBR } },
              { L"_rmaos", { NIFUtil::TextureSlots::ENVMASK, NIFUtil::TextureType::RMAOS } },
              { L"_envmask", { NIFUtil::TextureSlots::ENVMASK, NIFUtil::TextureType::ENVIRONMENTMASK } },
              { L"_em", { NIFUtil::TextureSlots::ENVMASK, NIFUtil::TextureType::ENVIRONMENTMASK } },
              { L"_m", { NIFUtil::TextureSlots::ENVMASK, NIFUtil::TextureType::ENVIRONMENTMASK } },
              { L"_e", { NIFUtil::TextureSlots::CUBEMAP, NIFUtil::TextureType::CUBEMAP } },
              { L"_p", { NIFUtil::TextureSlots::PARALLAX, NIFUtil::TextureType::HEIGHT } },
              { L"_sk", { NIFUtil::TextureSlots::GLOW, NIFUtil::TextureType::SKINTINT } },
              { L"_g", { NIFUtil::TextureSlots::GLOW, NIFUtil::TextureType::EMISSIVE } },
              { L"_msn", { NIFUtil::TextureSlots::NORMAL, NIFUtil::TextureType::NORMAL } },
              { L"_n", { NIFUtil::TextureSlots::NORMAL, NIFUtil::TextureType::NORMAL } },
              { L"_d", { NIFUtil::TextureSlots::DIFFUSE, NIFUtil::TextureType::DIFFUSE } },
              { L"mask", { NIFUtil::TextureSlots::DIFFUSE, NIFUtil::TextureType::DIFFUSE } } };

    return textureSuffixMap;
}

auto NIFUtil::getStrFromTexType(const TextureType& type) -> string
{
    static unordered_map<TextureType, string> strFromTexMap = { { TextureType::DIFFUSE, "diffuse" },
        { TextureType::NORMAL, "normal" }, { TextureType::MODELSPACENORMAL, "model space normal" },
        { TextureType::EMISSIVE, "emissive" }, { TextureType::SKINTINT, "skin tint" },
        { TextureType::SUBSURFACECOLOR, "subsurface color" }, { TextureType::HEIGHT, "height" },
        { TextureType::HEIGHTPBR, "height pbr" }, { TextureType::CUBEMAP, "cubemap" },
        { TextureType::ENVIRONMENTMASK, "environment mask" }, { TextureType::COMPLEXMATERIAL, "complex material" },
        { TextureType::RMAOS, "rmaos" }, { TextureType::SUBSURFACETINT, "subsurface tint" },
        { TextureType::INNERLAYER, "inner layer" }, { TextureType::FUZZPBR, "fuzz pbr" },
        { TextureType::COATNORMALROUGHNESS, "coat normal roughness" }, { TextureType::BACKLIGHT, "backlight" },
        { TextureType::SPECULAR, "specular" }, { TextureType::HAIR_FLOWMAP, "hair flowmap" },
        { TextureType::SUBSURFACEPBR, "subsurface pbr" }, { TextureType::UNKNOWN, "unknown" } };

    if (strFromTexMap.find(type) != strFromTexMap.end()) {
        return strFromTexMap[type];
    }

    return strFromTexMap[TextureType::UNKNOWN];
}

auto NIFUtil::getTexTypeFromStr(const string& type) -> TextureType
{
    static unordered_map<string, TextureType> texFromStrMap = { { "diffuse", TextureType::DIFFUSE },
        { "normal", TextureType::NORMAL }, { "model space normal", TextureType::MODELSPACENORMAL },
        { "emissive", TextureType::EMISSIVE }, { "skin tint", TextureType::SKINTINT },
        { "subsurface color", TextureType::SUBSURFACECOLOR }, { "height", TextureType::HEIGHT },
        { "height pbr", TextureType::HEIGHTPBR }, { "cubemap", TextureType::CUBEMAP },
        { "environment mask", TextureType::ENVIRONMENTMASK }, { "complex material", TextureType::COMPLEXMATERIAL },
        { "rmaos", TextureType::RMAOS }, { "subsurface tint", TextureType::SUBSURFACETINT },
        { "inner layer", TextureType::INNERLAYER }, { "fuzz pbr", TextureType::FUZZPBR },
        { "coat normal roughness", TextureType::COATNORMALROUGHNESS }, { "backlight", TextureType::BACKLIGHT },
        { "specular", TextureType::SPECULAR }, { "hair flowmap", TextureType::HAIR_FLOWMAP },
        { "subsurface pbr", TextureType::SUBSURFACEPBR }, { "unknown", TextureType::UNKNOWN } };

    const auto searchKey = boost::to_lower_copy(type);
    if (texFromStrMap.find(type) != texFromStrMap.end()) {
        return texFromStrMap[type];
    }

    return texFromStrMap["unknown"];
}

auto NIFUtil::getSlotFromTexType(const TextureType& type) -> TextureSlots
{
    static unordered_map<TextureType, TextureSlots> texTypeToSlotMap
        = { { TextureType::DIFFUSE, TextureSlots::DIFFUSE }, { TextureType::NORMAL, TextureSlots::NORMAL },
              { TextureType::MODELSPACENORMAL, TextureSlots::NORMAL }, { TextureType::EMISSIVE, TextureSlots::GLOW },
              { TextureType::SKINTINT, TextureSlots::GLOW }, { TextureType::SUBSURFACECOLOR, TextureSlots::GLOW },
              { TextureType::HEIGHT, TextureSlots::PARALLAX }, { TextureType::HEIGHTPBR, TextureSlots::PARALLAX },
              { TextureType::CUBEMAP, TextureSlots::CUBEMAP }, { TextureType::ENVIRONMENTMASK, TextureSlots::ENVMASK },
              { TextureType::COMPLEXMATERIAL, TextureSlots::ENVMASK }, { TextureType::RMAOS, TextureSlots::ENVMASK },
              { TextureType::SUBSURFACETINT, TextureSlots::MULTILAYER },
              { TextureType::INNERLAYER, TextureSlots::MULTILAYER }, { TextureType::FUZZPBR, TextureSlots::MULTILAYER },
              { TextureType::COATNORMALROUGHNESS, TextureSlots::MULTILAYER },
              { TextureType::BACKLIGHT, TextureSlots::BACKLIGHT }, { TextureType::SPECULAR, TextureSlots::BACKLIGHT },
              { TextureType::HAIR_FLOWMAP, TextureSlots::BACKLIGHT },
              { TextureType::SUBSURFACEPBR, TextureSlots::BACKLIGHT },
              { TextureType::UNKNOWN, TextureSlots::UNKNOWN } };

    if (texTypeToSlotMap.find(type) != texTypeToSlotMap.end()) {
        return texTypeToSlotMap[type];
    }

    return texTypeToSlotMap[TextureType::UNKNOWN];
}

auto NIFUtil::getDefaultsFromSuffix(const std::filesystem::path& path)
    -> tuple<NIFUtil::TextureSlots, NIFUtil::TextureType>
{
    const auto& suffixMap = getTexSuffixMap();

    // Get the texture suffix
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    const auto& pathStr = pathWithoutExtension.wstring();

    for (const auto& [suffix, slot] : suffixMap) {
        if (boost::iends_with(pathStr, suffix)) {
            // check if PBR in prefix
            if (get<1>(slot) == TextureType::HEIGHT && boost::istarts_with(pathStr, L"textures\\pbr")) {
                // This is a PBR heightmap so it gets a different texture type
                return { TextureSlots::PARALLAX, TextureType::HEIGHTPBR };
            }

            return slot;
        }
    }

    // Default return diffuse
    return { TextureSlots::UNKNOWN, TextureType::UNKNOWN };
}

auto NIFUtil::getTexTypesStr() -> vector<string>
{
    static const vector<string> texTypesStr = { "diffuse", "normal", "model space normal", "emissive", "skin tint",
        "subsurface color", "height", "height pbr", "cubemap", "environment mask", "complex material", "rmaos",
        "subsurface tint", "inner layer", "fuzz pbr", "coat normal roughness", "backlight", "specular",
        "subsurface pbr", "unknown" };

    return texTypesStr;
}

extern "C" auto loadNifWithSEH(nifly::NifFile* pNif, std::istream* pStream) -> bool
{
    __try {
        pNif->Load(*pStream);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Catch the access violation or other SEH exception
        return false;
    }
}

auto NIFUtil::loadNIFFromBytes(const std::vector<std::byte>& nifBytes, const bool& runChecks) -> nifly::NifFile
{
    // NIF file object
    NifFile nif;

    // Get NIF Bytes
    if (nifBytes.empty()) {
        throw runtime_error("File is empty");
    }

    // Convert Byte Vector to Stream
    // Using reinterpret_cast to convert from std::byte to char is more efficient due to less copies
    boost::iostreams::array_source nifArraySource(
        reinterpret_cast<const char*>(nifBytes.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        nifBytes.size());
    boost::iostreams::stream<boost::iostreams::array_source> nifStream(nifArraySource);

    if (!loadNifWithSEH(&nif, &nifStream)) {
        throw runtime_error("Failed to load NIF file");
    }

    if (!nif.IsValid() || !nif.GetHeader().IsValid()) {
        throw runtime_error("NIF did not load properly");
    }

    if (!runChecks) {
        return nif;
    }

    // Check shapes
    const auto shapes = nif.GetShapes();
    for (const auto& shape : shapes) {
        if (shape == nullptr) {
            throw runtime_error("NIF contains a null shape");
        }

        auto* nifShader = nif.GetShader(shape);
        if (nifShader != nullptr && nifShader->HasTextureSet()) {
            auto* txstRec = nif.GetHeader().GetBlock(nifShader->TextureSetRef());
            if (txstRec == nullptr) {
                throw runtime_error("NIF contains reference to texture set that does not exist");
            }
        }

        // Check for any non-ascii chars
        for (uint32_t slot = 0; slot < NUM_TEXTURE_SLOTS; slot++) {
            string texture;
            nif.GetTextureSlot(shape, texture, slot);

            if (!ParallaxGenUtil::containsOnlyAscii(texture)) {
                // NIFs cannot have non-ascii chars in their texture slots
                throw runtime_error("NIF contains non-ascii characters in texture slot(s)");
            }
        }
    }

    return nif;
}

auto NIFUtil::setShaderType(nifly::NiShader* nifShader, const nifly::BSLightingShaderPropertyShaderType& type) -> bool
{
    if (nifShader->GetShaderType() != type) {
        nifShader->SetShaderType(type);
        return true;
    }

    return false;
}

auto NIFUtil::setShaderFloat(float& value, const float& newValue) -> bool
{
    if (fabs(value - newValue) > MIN_FLOAT_COMPARISON) {
        value = newValue;
        return true;
    }

    return false;
}

auto NIFUtil::setShaderVec2(nifly::Vector2& value, const nifly::Vector2& newValue) -> bool
{
    if (value != newValue) {
        value = newValue;
        return true;
    }

    return false;
}

// Shader flag helpers
auto NIFUtil::hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag)
    -> bool
{
    return (nifShaderBSLSP->shaderFlags1 & flag) != 0U;
}

auto NIFUtil::hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag)
    -> bool
{
    return (nifShaderBSLSP->shaderFlags2 & flag) != 0U;
}

auto NIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag)
    -> bool
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 |= flag;
        return true;
    }

    return false;
}

auto NIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag)
    -> bool
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 |= flag;
        return true;
    }

    return false;
}

auto NIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag)
    -> bool
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 &= ~flag;
        return true;
    }

    return false;
}

auto NIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag)
    -> bool
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 &= ~flag;
        return true;
    }

    return false;
}

auto NIFUtil::configureShaderFlag(
    nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag, const bool& enable) -> bool
{
    bool changed = false;
    if (enable) {
        changed |= setShaderFlag(nifShaderBSLSP, flag);
    } else {
        changed |= clearShaderFlag(nifShaderBSLSP, flag);
    }

    return changed;
}

auto NIFUtil::configureShaderFlag(
    nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag, const bool& enable) -> bool
{
    bool changed = false;
    if (enable) {
        changed |= setShaderFlag(nifShaderBSLSP, flag);
    } else {
        changed |= clearShaderFlag(nifShaderBSLSP, flag);
    }

    return changed;
}

// Texture slot helpers
auto NIFUtil::setTextureSlot(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot, const std::wstring& texturePath) -> bool
{
    auto texturePathStr = ParallaxGenUtil::utf16toASCII(texturePath);
    return setTextureSlot(nif, nifShape, slot, texturePathStr);
}

auto NIFUtil::setTextureSlot(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot, const string& texturePath) -> bool
{
    string existingTex;
    nif->GetTextureSlot(nifShape, existingTex, static_cast<unsigned int>(slot));
    if (!boost::iequals(existingTex, texturePath)) {
        auto newTex = texturePath;
        nif->SetTextureSlot(nifShape, newTex, static_cast<unsigned int>(slot));
        return true;
    }

    return false;
}

auto NIFUtil::setTextureSlots(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const std::array<std::wstring, NUM_TEXTURE_SLOTS>& newSlots) -> bool
{
    bool changed = false;
    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        changed |= setTextureSlot(nif, nifShape, static_cast<TextureSlots>(i), newSlots.at(i));
    }

    return changed;
}

auto NIFUtil::getTextureSlot(nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot) -> string
{
    string texture;
    nif->GetTextureSlot(nifShape, texture, static_cast<unsigned int>(slot));
    boost::to_lower(texture);
    return texture;
}

auto NIFUtil::getTextureSlots(nifly::NifFile* nif, nifly::NiShape* nifShape) -> TextureSet
{
    array<wstring, NUM_TEXTURE_SLOTS> outSlots;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        string texture;
        const uint32_t result = nif->GetTextureSlot(nifShape, texture, i);
        boost::to_lower(texture);

        if (result == 0 || texture.empty()) {
            // no texture in Slot
            continue;
        }

        outSlots.at(i) = ParallaxGenUtil::asciitoUTF16(texture);
    }

    return outSlots;
}

auto NIFUtil::textureSetToStr(const TextureSet& set) -> TextureSetStr
{
    TextureSetStr outSet;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        outSet.at(i) = ParallaxGenUtil::utf16toUTF8(set.at(i));
    }

    return outSet;
}

auto NIFUtil::getTexBase(const std::filesystem::path& path, const TextureSlots& slot) -> std::wstring
{
    const auto& suffixMap = getTexSuffixMap();

    // Get the texture suffix
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    const auto& pathStr = pathWithoutExtension.wstring();

    if (slot == TextureSlots::UNKNOWN) {
        // just return path without extension
        return pathStr;
    }

    for (const auto& [suffix, curSlot] : suffixMap) {
        if (slot != get<0>(curSlot)) {
            continue;
        }

        if (boost::iends_with(pathStr, suffix)) {
            return pathStr.substr(0, pathStr.size() - suffix.size());
        }
    }

    return pathStr;
}

auto NIFUtil::getTexMatch(const wstring& base, const TextureType& desiredType,
    const map<wstring, unordered_set<PGTexture, PGTextureHasher>>& searchMap) -> vector<PGTexture>
{
    // Binary search on base list
    const wstring baseLower = boost::to_lower_copy(base);
    const auto it = searchMap.find(baseLower);

    if (it != searchMap.end()) {
        // Found a match
        if (!boost::equals(it->first, baseLower)) {
            // No exact match
            return {};
        }

        if (it->second.empty()) {
            // No textures
            return {};
        }

        vector<PGTexture> outTex;
        for (const auto& texture : it->second) {
            if (texture.type == desiredType) {
                outTex.push_back(texture);
            } else {
                continue;
            }
        }

        return outTex;
    }

    return {};
}

auto NIFUtil::getSearchPrefixes(NifFile const& nif, nifly::NiShape* nifShape, const bool& findBaseSlots)
    -> array<wstring, NUM_TEXTURE_SLOTS>
{
    array<wstring, NUM_TEXTURE_SLOTS> outPrefixes;

    // Loop through each texture Slot
    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        string texture;
        const uint32_t result = nif.GetTextureSlot(nifShape, texture, i);

        if (result == 0 || texture.empty()) {
            // no texture in Slot
            continue;
        }

        // Get default suffixes
        wstring texBase;
        if (findBaseSlots) {
            // Get the base texture name without suffix
            texBase = getTexBase(ParallaxGenUtil::asciitoUTF16(texture), static_cast<TextureSlots>(i));
        } else {
            // Get the full texture name
            texBase = getTexBase(ParallaxGenUtil::asciitoUTF16(texture));
        }

        outPrefixes.at(i) = texBase;
    }

    return outPrefixes;
}

auto NIFUtil::getSearchPrefixes(const array<wstring, NUM_TEXTURE_SLOTS>& oldSlots, const bool& findBaseSlots)
    -> array<wstring, NUM_TEXTURE_SLOTS>
{
    array<wstring, NUM_TEXTURE_SLOTS> outSlots;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        if (oldSlots.at(i).empty()) {
            continue;
        }

        wstring texBase;
        if (findBaseSlots) {
            // Get the base texture name without suffix
            texBase = getTexBase(oldSlots.at(i), static_cast<TextureSlots>(i));
        } else {
            // Get the full texture name
            texBase = getTexBase(oldSlots.at(i));
        }
        outSlots.at(i) = texBase;
    }

    return outSlots;
}

auto NIFUtil::getShapesWithBlockIDs(const nifly::NifFile* nif) -> unordered_map<nifly::NiShape*, int>
{
    if (nif == nullptr) {
        throw runtime_error("NIF is null");
    }

    vector<NiObject*> tree;
    nif->GetTree(tree);
    unordered_map<NiShape*, int> shapes;
    int oldIndex3D = 0;
    for (auto& obj : tree) {
        auto* const curShape = dynamic_cast<NiShape*>(obj);
        if (curShape != nullptr) {
            shapes[curShape] = oldIndex3D++;
            continue;
        }

        // other stuff that should increment oldIndex3D
        if (dynamic_cast<NiParticleSystem*>(obj) != nullptr) {
            // Particle system, increment index3d
            oldIndex3D++;
        }
    }

    return shapes;
}

auto NIFUtil::isPatchableShape(nifly::NifFile& nif, nifly::NiShape& nifShape) -> bool
{
    const string shapeBlockName = nifShape.GetBlockName();

    // Check if shape should be patched or not
    if (shapeBlockName != "NiTriShape" && shapeBlockName != "BSTriShape" && shapeBlockName != "BSLODTriShape"
        && shapeBlockName != "BSMeshLODTriShape" && shapeBlockName != "BSDynamicTriShape") {
        return false;
    }

    // get NIFShader type
    if (!nifShape.HasShaderProperty()) {
        return false;
    }

    // get NIFShader from shape
    NiShader* nifShader = nif.GetShader(&nifShape);
    return nifShader != nullptr;
}

auto NIFUtil::isShaderPatchableShape(nifly::NifFile& nif, nifly::NiShape& nifShape) -> bool
{
    const string shapeBlockName = nifShape.GetBlockName();

    if (!isPatchableShape(nif, nifShape)) {
        return false;
    }

    // check that NIFShader is a BSLightingShaderProperty
    NiShader* nifShader = nif.GetShader(&nifShape);
    const string nifShaderName = nifShader->GetBlockName();
    if (nifShaderName != "BSLightingShaderProperty") {
        return false;
    }

    // check that NIFShader has a texture set
    if (!nifShader->HasTextureSet()) {
        return false;
    }

    // Check if PG_IGNORE is set on shader or shape
    auto checkIgnoreFlag = [&nif](auto& extraDataRefs) -> bool {
        for (const auto& extraDataRef : extraDataRefs) {
            auto* const curBlock = nif.GetHeader().GetBlock(extraDataRef);
            auto* const booleanBlock = dynamic_cast<NiBooleanExtraData*>(curBlock);
            if (booleanBlock != nullptr && booleanBlock->name == "PG_IGNORE" && booleanBlock->booleanData) {
                return true; // PG_IGNORE found and set to true
            }
        }
        return false;
    };

    return !checkIgnoreFlag(nifShader->extraDataRefs) && !checkIgnoreFlag(nifShape.extraDataRefs);
}
