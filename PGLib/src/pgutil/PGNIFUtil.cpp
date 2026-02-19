#include "pgutil/PGNIFUtil.hpp"

#include "BasicTypes.hpp"
#include "ExtraData.hpp"
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

auto PGNIFUtil::getTexSuffixMap() -> map<wstring,
                                         tuple<PGEnums::TextureSlots,
                                               PGEnums::TextureType>>
{
    static const map<wstring, tuple<PGEnums::TextureSlots, PGEnums::TextureType>> textureSuffixMap
        = {{L"_bl", {PGEnums::TextureSlots::BACKLIGHT, PGEnums::TextureType::BACKLIGHT}},
           {L"_b", {PGEnums::TextureSlots::BACKLIGHT, PGEnums::TextureType::BACKLIGHT}},
           {L"_flow", {PGEnums::TextureSlots::BACKLIGHT, PGEnums::TextureType::HAIR_FLOWMAP}},
           {L"_cnr", {PGEnums::TextureSlots::MULTILAYER, PGEnums::TextureType::COATNORMALROUGHNESS}},
           {L"_s", {PGEnums::TextureSlots::MULTILAYER, PGEnums::TextureType::SUBSURFACETINT}},
           {L"_i", {PGEnums::TextureSlots::MULTILAYER, PGEnums::TextureType::INNERLAYER}},
           {L"_f", {PGEnums::TextureSlots::MULTILAYER, PGEnums::TextureType::FUZZPBR}},
           {L"_rmaos", {PGEnums::TextureSlots::ENVMASK, PGEnums::TextureType::RMAOS}},
           {L"_envmask", {PGEnums::TextureSlots::ENVMASK, PGEnums::TextureType::ENVIRONMENTMASK}},
           {L"_em", {PGEnums::TextureSlots::ENVMASK, PGEnums::TextureType::ENVIRONMENTMASK}},
           {L"_m", {PGEnums::TextureSlots::ENVMASK, PGEnums::TextureType::ENVIRONMENTMASK}},
           {L"_e", {PGEnums::TextureSlots::CUBEMAP, PGEnums::TextureType::CUBEMAP}},
           {L"_p", {PGEnums::TextureSlots::PARALLAX, PGEnums::TextureType::HEIGHT}},
           {L"_sk", {PGEnums::TextureSlots::GLOW, PGEnums::TextureType::SKINTINT}},
           {L"_g", {PGEnums::TextureSlots::GLOW, PGEnums::TextureType::EMISSIVE}},
           {L"_msn", {PGEnums::TextureSlots::NORMAL, PGEnums::TextureType::NORMAL}},
           {L"_n", {PGEnums::TextureSlots::NORMAL, PGEnums::TextureType::NORMAL}},
           {L"_d", {PGEnums::TextureSlots::DIFFUSE, PGEnums::TextureType::DIFFUSE}},
           {L"mask", {PGEnums::TextureSlots::DIFFUSE, PGEnums::TextureType::DIFFUSE}}};

    return textureSuffixMap;
}

auto PGNIFUtil::getSlotFromTexType(const PGEnums::TextureType& type) -> PGEnums::TextureSlots
{
    static std::unordered_map<PGEnums::TextureType, PGEnums::TextureSlots> texTypeToSlotMap
        = {{PGEnums::TextureType::DIFFUSE, PGEnums::TextureSlots::DIFFUSE},
           {PGEnums::TextureType::NORMAL, PGEnums::TextureSlots::NORMAL},
           {PGEnums::TextureType::MODELSPACENORMAL, PGEnums::TextureSlots::NORMAL},
           {PGEnums::TextureType::EMISSIVE, PGEnums::TextureSlots::GLOW},
           {PGEnums::TextureType::SKINTINT, PGEnums::TextureSlots::GLOW},
           {PGEnums::TextureType::SUBSURFACECOLOR, PGEnums::TextureSlots::GLOW},
           {PGEnums::TextureType::HEIGHT, PGEnums::TextureSlots::PARALLAX},
           {PGEnums::TextureType::HEIGHTPBR, PGEnums::TextureSlots::PARALLAX},
           {PGEnums::TextureType::CUBEMAP, PGEnums::TextureSlots::CUBEMAP},
           {PGEnums::TextureType::ENVIRONMENTMASK, PGEnums::TextureSlots::ENVMASK},
           {PGEnums::TextureType::COMPLEXMATERIAL, PGEnums::TextureSlots::ENVMASK},
           {PGEnums::TextureType::RMAOS, PGEnums::TextureSlots::ENVMASK},
           {PGEnums::TextureType::SUBSURFACETINT, PGEnums::TextureSlots::MULTILAYER},
           {PGEnums::TextureType::INNERLAYER, PGEnums::TextureSlots::MULTILAYER},
           {PGEnums::TextureType::FUZZPBR, PGEnums::TextureSlots::MULTILAYER},
           {PGEnums::TextureType::COATNORMALROUGHNESS, PGEnums::TextureSlots::MULTILAYER},
           {PGEnums::TextureType::BACKLIGHT, PGEnums::TextureSlots::BACKLIGHT},
           {PGEnums::TextureType::SPECULAR, PGEnums::TextureSlots::BACKLIGHT},
           {PGEnums::TextureType::HAIR_FLOWMAP, PGEnums::TextureSlots::BACKLIGHT},
           {PGEnums::TextureType::SUBSURFACEPBR, PGEnums::TextureSlots::BACKLIGHT},
           {PGEnums::TextureType::UNKNOWN, PGEnums::TextureSlots::UNKNOWN}};

    if (texTypeToSlotMap.find(type) != texTypeToSlotMap.end()) {
        return texTypeToSlotMap[type];
    }

    return texTypeToSlotMap[PGEnums::TextureType::UNKNOWN];
}

auto PGNIFUtil::getDefaultsFromSuffix(const std::filesystem::path& path) -> std::tuple<PGEnums::TextureSlots,
                                                                                       PGEnums::TextureType>
{
    const auto& suffixMap = getTexSuffixMap();

    // Get the texture suffix
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    const auto& pathStr = pathWithoutExtension.wstring();

    for (const auto& [suffix, slot] : suffixMap) {
        if (boost::iends_with(pathStr, suffix)) {
            // check if PBR in prefix
            if (get<1>(slot) == PGEnums::TextureType::HEIGHT && boost::istarts_with(pathStr, L"textures\\pbr")) {
                // This is a PBR heightmap so it gets a different texture type
                return {PGEnums::TextureSlots::PARALLAX, PGEnums::TextureType::HEIGHTPBR};
            }

            return slot;
        }
    }

    // Default return diffuse
    return {PGEnums::TextureSlots::UNKNOWN, PGEnums::TextureType::UNKNOWN};
}

extern "C" auto loadNifWithSEH(nifly::NifFile* pNif,
                               std::istream* pStream) -> bool
{
    __try {
        pNif->Load(*pStream);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Catch the access violation or other SEH exception
        return false;
    }
}

auto PGNIFUtil::loadNIFFromBytes(const std::vector<std::byte>& nifBytes,
                                 const bool& runChecks) -> nifly::NifFile
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

            if (!StringUtil::containsOnlyAscii(texture)) {
                // NIFs cannot have non-ascii chars in their texture slots
                throw runtime_error("NIF contains non-ascii characters in texture slot(s)");
            }
        }
    }

    return nif;
}

auto PGNIFUtil::setShaderType(nifly::NiShader* nifShader,
                              const nifly::BSLightingShaderPropertyShaderType& type) -> bool
{
    if (nifShader->GetShaderType() != type) {
        nifShader->SetShaderType(type);
        return true;
    }

    return false;
}

auto PGNIFUtil::setShaderFloat(float& value,
                               const float& newValue) -> bool
{
    if (fabs(value - newValue) > MIN_FLOAT_COMPARISON) {
        value = newValue;
        return true;
    }

    return false;
}

auto PGNIFUtil::setShaderVec2(nifly::Vector2& value,
                              const nifly::Vector2& newValue) -> bool
{
    if (value != newValue) {
        value = newValue;
        return true;
    }

    return false;
}

// Shader flag helpers
auto PGNIFUtil::hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags1& flag) -> bool
{
    return (nifShaderBSLSP->shaderFlags1 & flag) != 0U;
}

auto PGNIFUtil::hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag) -> bool
{
    return (nifShaderBSLSP->shaderFlags2 & flag) != 0U;
}

auto PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags1& flag) -> bool
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 |= flag;
        return true;
    }

    return false;
}

auto PGNIFUtil::setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                              const nifly::SkyrimShaderPropertyFlags2& flag) -> bool
{
    if (!hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 |= flag;
        return true;
    }

    return false;
}

auto PGNIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                const nifly::SkyrimShaderPropertyFlags1& flag) -> bool
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags1 &= ~flag;
        return true;
    }

    return false;
}

auto PGNIFUtil::clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                const nifly::SkyrimShaderPropertyFlags2& flag) -> bool
{
    if (hasShaderFlag(nifShaderBSLSP, flag)) {
        nifShaderBSLSP->shaderFlags2 &= ~flag;
        return true;
    }

    return false;
}

auto PGNIFUtil::configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                    const nifly::SkyrimShaderPropertyFlags1& flag,
                                    const bool& enable) -> bool
{
    bool changed = false;
    if (enable) {
        changed |= setShaderFlag(nifShaderBSLSP, flag);
    } else {
        changed |= clearShaderFlag(nifShaderBSLSP, flag);
    }

    return changed;
}

auto PGNIFUtil::configureShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP,
                                    const nifly::SkyrimShaderPropertyFlags2& flag,
                                    const bool& enable) -> bool
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
auto PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const std::wstring& texturePath) -> bool
{
    auto texturePathStr = StringUtil::utf16toASCII(texturePath);
    return setTextureSlot(nif, nifShape, slot, texturePathStr);
}

auto PGNIFUtil::setTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot,
                               const string& texturePath) -> bool
{
    string existingTex;
    nif->GetTextureSlot(nifShape, existingTex, static_cast<unsigned int>(slot));
    if (!StringUtil::asciiFastIEquals(existingTex, texturePath)) {
        auto newTex = texturePath;
        nif->SetTextureSlot(nifShape, newTex, static_cast<unsigned int>(slot));
        return true;
    }

    return false;
}

auto PGNIFUtil::setTextureSlots(nifly::NifFile* nif,
                                nifly::NiShape* nifShape,
                                const std::array<std::wstring,
                                                 NUM_TEXTURE_SLOTS>& newSlots) -> bool
{
    bool changed = false;
    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        changed |= setTextureSlot(nif, nifShape, static_cast<PGEnums::TextureSlots>(i), newSlots.at(i));
    }

    return changed;
}

auto PGNIFUtil::getTextureSlot(nifly::NifFile* nif,
                               nifly::NiShape* nifShape,
                               const PGEnums::TextureSlots& slot) -> std::string
{
    string texture;
    nif->GetTextureSlot(nifShape, texture, static_cast<unsigned int>(slot));
    StringUtil::toLowerASCIIFastInPlace(texture);
    return texture;
}

auto PGNIFUtil::getTextureSlots(nifly::NifFile* nif,
                                nifly::NiShape* nifShape) -> PGTypes::TextureSet
{
    array<wstring, NUM_TEXTURE_SLOTS> outSlots;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        string texture;
        const uint32_t result = nif->GetTextureSlot(nifShape, texture, i);
        StringUtil::toLowerASCIIFastInPlace(texture);

        if (result == 0 || texture.empty()) {
            // no texture in Slot
            continue;
        }

        outSlots.at(i) = StringUtil::asciitoUTF16(texture);
    }

    return outSlots;
}

auto PGNIFUtil::textureSetToStr(const PGTypes::TextureSet& set) -> PGTypes::TextureSetStr
{
    PGTypes::TextureSetStr outSet;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        outSet.at(i) = StringUtil::utf16toUTF8(set.at(i));
    }

    return outSet;
}

auto PGNIFUtil::getTexBase(const std::filesystem::path& path,
                           const PGEnums::TextureSlots& slot) -> std::wstring
{
    const auto& suffixMap = getTexSuffixMap();

    // Get the texture suffix
    const auto pathWithoutExtension = path.parent_path() / path.stem();
    auto pathStr = pathWithoutExtension.wstring();
    // faster ascii lower is okay here because ALL textures must be purely ascii by the time they reach here
    StringUtil::toLowerASCIIFastInPlace(pathStr);

    if (slot == PGEnums::TextureSlots::UNKNOWN) {
        // just return path without extension
        return pathStr;
    }

    for (const auto& [suffix, curSlot] : suffixMap) {
        if (slot != get<0>(curSlot)) {
            continue;
        }

        if (pathStr.ends_with(suffix)) {
            return pathStr.substr(0, pathStr.size() - suffix.size());
        }
    }

    return pathStr;
}

auto PGNIFUtil::getTexMatch(const wstring& base,
                            const PGEnums::TextureType& desiredType,
                            const map<wstring,
                                      unordered_set<PGTypes::PGTexture,
                                                    PGTypes::PGTextureHasher>>& searchMap) -> vector<PGTypes::PGTexture>
{
    // Binary search on base list
    const wstring baseLower = StringUtil::toLowerASCIIFast(base);
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

        vector<PGTypes::PGTexture> outTex;
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

auto PGNIFUtil::getSearchPrefixes(NifFile const& nif,
                                  nifly::NiShape* nifShape,
                                  const bool& findBaseSlots) -> array<wstring,
                                                                      NUM_TEXTURE_SLOTS>
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
            texBase = getTexBase(StringUtil::asciitoUTF16(texture), static_cast<PGEnums::TextureSlots>(i));
        } else {
            // Get the full texture name
            texBase = getTexBase(StringUtil::asciitoUTF16(texture));
        }

        outPrefixes.at(i) = texBase;
    }

    return outPrefixes;
}

auto PGNIFUtil::getSearchPrefixes(const array<wstring,
                                              NUM_TEXTURE_SLOTS>& oldSlots,
                                  const bool& findBaseSlots) -> array<wstring,
                                                                      NUM_TEXTURE_SLOTS>
{
    array<wstring, NUM_TEXTURE_SLOTS> outSlots;

    for (uint32_t i = 0; i < NUM_TEXTURE_SLOTS; i++) {
        if (oldSlots.at(i).empty()) {
            continue;
        }

        wstring texBase;
        if (findBaseSlots) {
            // Get the base texture name without suffix
            texBase = getTexBase(oldSlots.at(i), static_cast<PGEnums::TextureSlots>(i));
        } else {
            // Get the full texture name
            texBase = getTexBase(oldSlots.at(i));
        }
        outSlots.at(i) = texBase;
    }

    return outSlots;
}

auto PGNIFUtil::getShapesWithBlockIDs(const nifly::NifFile* nif) -> unordered_map<nifly::NiShape*,
                                                                                  int>
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

auto PGNIFUtil::isPatchableShape(nifly::NifFile& nif,
                                 nifly::NiShape& nifShape) -> bool
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

auto PGNIFUtil::isShaderPatchableShape(nifly::NifFile& nif,
                                       nifly::NiShape& nifShape) -> bool
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
