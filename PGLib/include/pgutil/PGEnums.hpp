#pragma once

#include "util/EnumStringHelper.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace PGEnums {
// These need to be in the order of worst shader to best shader
enum class ShapeShader : uint8_t { UNKNOWN, NONE, VANILLAPARALLAX, COMPLEXMATERIAL, TRUEPBR };

static constexpr std::array<EnumStringHelper::EnumStringEntry<ShapeShader>, 23> SHAPESHADER_TABLE {{
    {.value = ShapeShader::NONE, .name = "Default"},
    {.value = ShapeShader::UNKNOWN, .name = "Unknown"},
    {.value = ShapeShader::TRUEPBR, .name = "PBR"},
    {.value = ShapeShader::COMPLEXMATERIAL, .name = "Complex Material"},
    {.value = ShapeShader::VANILLAPARALLAX, .name = "Parallax"},
}};

/// @brief get a string that represents the given shader
/// @param[in] shader shader type
/// @return string containing the name of the shader
auto getStrFromShader(const ShapeShader& shader) -> std::string;

/// @brief get the shader type from a string
/// @param[in] shader string containing the name of the shader
/// @return the shader type
auto getShaderFromStr(const std::string& shader) -> ShapeShader;

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
    HAIR_FLOWMAP,
    SPECULAR,
    SUBSURFACEPBR,
    UNKNOWN
};

static constexpr std::array<EnumStringHelper::EnumStringEntry<TextureType>, 21> TEXTURETYPE_TABLE {{
    {.value = TextureType::DIFFUSE, .name = "diffuse"},
    {.value = TextureType::NORMAL, .name = "normal"},
    {.value = TextureType::MODELSPACENORMAL, .name = "model space normal"},
    {.value = TextureType::EMISSIVE, .name = "emissive"},
    {.value = TextureType::SKINTINT, .name = "skin tint"},
    {.value = TextureType::SUBSURFACECOLOR, .name = "subsurface color"},
    {.value = TextureType::HEIGHT, .name = "height"},
    {.value = TextureType::HEIGHTPBR, .name = "height pbr"},
    {.value = TextureType::CUBEMAP, .name = "cubemap"},
    {.value = TextureType::ENVIRONMENTMASK, .name = "environment mask"},
    {.value = TextureType::COMPLEXMATERIAL, .name = "complex material"},
    {.value = TextureType::RMAOS, .name = "rmaos"},
    {.value = TextureType::SUBSURFACETINT, .name = "subsurface tint"},
    {.value = TextureType::INNERLAYER, .name = "inner layer"},
    {.value = TextureType::FUZZPBR, .name = "fuzz pbr"},
    {.value = TextureType::COATNORMALROUGHNESS, .name = "coat normal roughness"},
    {.value = TextureType::BACKLIGHT, .name = "backlight"},
    {.value = TextureType::SPECULAR, .name = "specular"},
    {.value = TextureType::HAIR_FLOWMAP, .name = "hair flowmap"},
    {.value = TextureType::SUBSURFACEPBR, .name = "subsurface pbr"},
}};

auto getStrFromTexType(const TextureType& type) -> std::string;

auto getTexTypeFromStr(const std::string& type) -> TextureType;

auto getTexTypesStr() -> std::vector<std::string>;

enum class TextureAttribute : uint8_t { CM_ENVMASK, CM_GLOSSINESS, CM_METALNESS, CM_HEIGHT };

}
