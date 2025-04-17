#pragma once

#include <Geometry.hpp>
#include <NifFile.hpp>
#include <Shaders.hpp>
#include <array>
#include <tuple>

constexpr unsigned NUM_TEXTURE_SLOTS = 9;

namespace NIFUtil {
using TextureSet = std::array<std::wstring, NUM_TEXTURE_SLOTS>;
using TextureSetStr = std::array<std::string, NUM_TEXTURE_SLOTS>;

struct TextureSetHash {
    auto operator()(const TextureSet& ts) const -> std::size_t
    {
        static constexpr auto MAGIC_HASH = 0x9e3779b9; // Golden ratio
        static constexpr auto BIT_MIX_LEFT = 6;
        static constexpr auto BIT_MIX_RIGHT = 2;
        std::size_t h = 0;
        for (const auto& s : ts) {
            h ^= std::hash<std::wstring> {}(s) + MAGIC_HASH + (h << BIT_MIX_LEFT)
                + (h >> BIT_MIX_RIGHT); // hash combine
        }
        return h;
    }
};

static constexpr float MIN_FLOAT_COMPARISON = 10e-05F;

// These need to be in the order of worst shader to best shader
enum class ShapeShader : uint8_t { UNKNOWN, NONE, VANILLAPARALLAX, COMPLEXMATERIAL, TRUEPBR };

/// @brief get a string that represents the given shader
/// @param[in] shader shader type
/// @return string containing the name of the shader
auto getStrFromShader(const ShapeShader& shader) -> std::string;

/// @brief get the shader type from a string
/// @param[in] shader string containing the name of the shader
/// @return the shader type
auto getShaderFromStr(const std::string& shader) -> ShapeShader;

auto getTextureSlotsFromStr(const std::string& slots) -> TextureSet;

auto getStrFromTextureSlots(const TextureSet& slots) -> std::string;

/// @brief zero-based index of texture in BSShaderTextureSet
/// there can be more than one type of textures assigned to a a texture slot, the slot name describes the default one
enum class TextureSlots : uint8_t {
    DIFFUSE,
    NORMAL,
    GLOW,
    PARALLAX,
    CUBEMAP,
    ENVMASK,
    MULTILAYER,
    BACKLIGHT,
    UNUSED,
    UNKNOWN
};

/// @brief All known types of textures
enum class TextureType : uint8_t {
    DIFFUSE,
    NORMAL,
    MODELSPACENORMAL,
    EMISSIVE,
    SKINTINT,
    SUBSURFACECOLOR,
    HEIGHT,
    HEIGHTPBR,
    CUBEMAP,
    ENVIRONMENTMASK,
    COMPLEXMATERIAL,
    RMAOS,
    SUBSURFACETINT,
    INNERLAYER,
    FUZZPBR,
    COATNORMALROUGHNESS,
    BACKLIGHT,
    SPECULAR,
    SUBSURFACEPBR,
    UNKNOWN
};

enum class TextureAttribute : uint8_t { CM_ENVMASK, CM_GLOSSINESS, CM_METALNESS, CM_HEIGHT };

auto getStrFromTexAttribute(const TextureAttribute& attribute) -> std::string;

auto getStrSetFromTexAttributeSet(const std::unordered_set<TextureAttribute>& attributeSet)
    -> std::unordered_set<std::string>;

auto getStrFromTexType(const TextureType& type) -> std::string;

auto getTexTypeFromStr(const std::string& type) -> TextureType;

auto getSlotFromTexType(const TextureType& type) -> TextureSlots;

/// @brief texture used by parallaxgen with type
struct PGTexture {
    /// @brief relative path in the data directory
    std::filesystem::path path;
    TextureType type {};

    // Equality operator
    auto operator==(const PGTexture& other) const -> bool { return path == other.path && type == other.type; }
};

struct PGTextureHasher {
    auto operator()(const PGTexture& texture) const -> size_t
    {
        // Hash the path and the texture type, and combine them
        const std::size_t pathHash = std::hash<std::filesystem::path>()(texture.path);
        const std::size_t typeHash = std::hash<int>()(static_cast<int>(texture.type));

        // Combine the hashes using bitwise XOR and bit shifting
        return pathHash ^ (typeHash << 1);
    }
};

auto getTexTypesStr() -> std::vector<std::string>;

/// @brief get the texture type that is assigned per default to a texture slot
/// @param[in] slot
/// @return texture type
auto getDefaultTextureType(const TextureSlots& slot) -> TextureType;

/// @brief load a Nif from memory
/// @param[in] nifBytes memory containing the NIF
/// @return the nif
auto loadNIFFromBytes(const std::vector<std::byte>& nifBytes) -> nifly::NifFile;

/// @brief get a map containing the known texture suffixes
/// @return the map containing the suffixes and the slot/type pairs
auto getTexSuffixMap() -> std::map<std::wstring, std::tuple<TextureSlots, TextureType>>;

/// @brief Deduct the texture type and slot usually used from the suffix of a texture
/// @param[in] path texture to check
/// @return pair of texture slot and type of that texture
auto getDefaultsFromSuffix(const std::filesystem::path& path) -> std::tuple<TextureSlots, TextureType>;

/// @brief set the shader type of a given shader
/// @param nifShader the shader
/// @param[in] type type that is set
auto setShaderType(nifly::NiShader* nifShader, const nifly::BSLightingShaderPropertyShaderType& type) -> bool;

/// @brief set a new value for a float value
/// @param value the value to change
/// @param[in] newValue the new value to set
auto setShaderFloat(float& value, const float& newValue) -> bool;

/// @brief set a new value for a vector float value
/// @param value the value to change
/// @param[in] newValue the new value
/// @return
auto setShaderVec2(nifly::Vector2& value, const nifly::Vector2& newValue) -> bool;

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
auto hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief check if a given flag is set for a shader
/// @param nifShaderBSLSP the shader to check
/// @param[in] flag the flag to check
/// @return if the flag is set
auto hasShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief set a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
auto setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief set a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
auto setShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief clear all shader flags 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag1 to clear
auto clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag) -> bool;

/// @brief clear all shader flags 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag2 to clear
auto clearShaderFlag(nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag) -> bool;

/// @brief set or unset a given shader flag 1 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
auto configureShaderFlag(
    nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags1& flag, const bool& enable) -> bool;

/// @brief set or unset a given shader flag 2 for a shader
/// @param nifShaderBSLSP the shader
/// @param[in] flag the flag to set
/// @param[in] enable enable or disable the flag
auto configureShaderFlag(
    nifly::BSShaderProperty* nifShaderBSLSP, const nifly::SkyrimShaderPropertyFlags2& flag, const bool& enable) -> bool;

/// @brief set the path of a texture slot for a shape, unicode variant
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
auto setTextureSlot(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot, const std::wstring& texturePath) -> bool;

/// @brief set the path of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot texture slot
/// @param[in] texturePath the path to set
auto setTextureSlot(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot, const std::string& texturePath) -> bool;

/// @brief set all paths of a texture slot for a shape
/// @param nif nif
/// @param nifShape the shape
/// @param[in] newSlots textures to set
auto setTextureSlots(
    nifly::NifFile* nif, nifly::NiShape* nifShape, const std::array<std::wstring, NUM_TEXTURE_SLOTS>& newSlots) -> bool;

/// @brief get the set texture for a slot
/// @param nif nif
/// @param nifShape the shape
/// @param[in] slot the slot
/// @return texture set in the slot
auto getTextureSlot(nifly::NifFile* nif, nifly::NiShape* nifShape, const TextureSlots& slot) -> std::string;

/// @brief get all set texture slots from a shape
/// @param nif nif
/// @param nifShape shape
/// @return array of textures set in the slots
auto getTextureSlots(nifly::NifFile* nif, nifly::NiShape* nifShape) -> TextureSet;

auto textureSetToStr(const TextureSet& set) -> TextureSetStr;

/// @brief get the texture name without suffix, i.e. without _n.dds
/// @param[in] texPath the path to get the base for
/// @return base path
auto getTexBase(const std::filesystem::path& texPath, const TextureSlots& slot = TextureSlots::UNKNOWN) -> std::wstring;

/// @brief get the matching textures for a given base path
/// @param[in] base base texture name
/// @param[in] desiredType the type to find
/// @param[in] searchMap base names without suffix mapped to a set of potential textures. strings and paths must all be
/// lowercase
/// @return vector of textures
auto getTexMatch(const std::wstring& base, const TextureType& desiredType,
    const std::map<std::wstring, std::unordered_set<PGTexture, PGTextureHasher>>& searchMap) -> std::vector<PGTexture>;

/// @brief Gets all the texture prefixes for a textureset from a nif shape, ie. _n.dds is removed etc. for each slot
/// @param[in] nif the nif
/// @param nifShape the shape
/// @return array of texture names without suffixes
auto getSearchPrefixes(nifly::NifFile const& nif, nifly::NiShape* nifShape, const bool& findBaseSlots = true)
    -> std::array<std::wstring, NUM_TEXTURE_SLOTS>;

/// @brief Gets all the texture prefixes for a texture set. ie. _n.dds is removed etc. for each slot
/// @param[in] oldSlots
/// @return array of texture names without suffixes
auto getSearchPrefixes(const std::array<std::wstring, NUM_TEXTURE_SLOTS>& oldSlots, const bool& findBaseSlots = true)
    -> std::array<std::wstring, NUM_TEXTURE_SLOTS>;

auto getShapesWithBlockIDs(const nifly::NifFile* nif) -> std::unordered_map<nifly::NiShape*, int>;

} // namespace NIFUtil
