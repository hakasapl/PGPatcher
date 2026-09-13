#pragma once

#include "util/EnumStringHelper.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief Namespace containing enumerations and conversion utilities for shader types, texture slots, and texture
 * attributes.
 */
namespace PGEnums {
// These need to be in the order of worst shader to best shader.
/// @brief Represents the shader type applied to a shape, ordered from worst to best quality.
enum class ShapeShader : uint8_t { Unknown, NONE, VANILLAPARALLAX, COMPLEXMATERIAL, TRUEPBR };

static constexpr std::array<EnumStringHelper::EnumStringEntry<ShapeShader>, 5> shapeShaderTable {
    {
        { .value = ShapeShader::NONE, .name = "Default" },
        { .value = ShapeShader::Unknown, .name = "Unknown" },
        { .value = ShapeShader::TRUEPBR, .name = "PBR" },
        { .value = ShapeShader::COMPLEXMATERIAL, .name = "Complex Material" },
        { .value = ShapeShader::VANILLAPARALLAX, .name = "Parallax" },
    },
};

/// @brief get a string that represents the given shader
/// @param[in] shader shader type
/// @return string containing the name of the shader
std::string strFromShader(const ShapeShader& shader);

/// @brief get the shader type from a string
/// @param[in] shader string containing the name of the shader
/// @return the shader type
ShapeShader shaderFromStr(const std::string& shader);

/// @brief zero-based index of texture in BSShaderTextureSet
/// there can be more than one type of textures assigned to a a texture slot, the slot name describes the default one
enum class TextureSlots : uint8_t {
    Diffuse,
    Normal,
    Glow,
    Parallax,
    Cubemap,
    EnvMask,
    MultiLayer,
    Backlight,
    Unused,
    Unknown,
};

/// @brief All known types of textures
enum class TextureType : uint8_t {
    Diffuse,
    Normal,
    ModelSpaceNormal,
    Emissive,
    SkinTint,
    SubsurfaceColor,
    Height,
    HeightPBR,
    Cubemap,
    EnvironmentMask,
    ComplexMaterial,
    RMAOS,
    SubsurfaceTint,
    InnerLayer,
    FuzzPBR,
    CoatNormalRoughness,
    Backlight,
    HairFlowMap,
    Specular,
    SubsurfacePBR,
    Unknown,
};

static constexpr std::array<EnumStringHelper::EnumStringEntry<TextureType>, 21> textureTypeTable {
    {
        { .value = TextureType::Diffuse, .name = "diffuse" },
        { .value = TextureType::Normal, .name = "normal" },
        { .value = TextureType::ModelSpaceNormal, .name = "model space normal" },
        { .value = TextureType::Emissive, .name = "emissive" },
        { .value = TextureType::SkinTint, .name = "skin tint" },
        { .value = TextureType::SubsurfaceColor, .name = "subsurface color" },
        { .value = TextureType::Height, .name = "height" },
        { .value = TextureType::HeightPBR, .name = "height pbr" },
        { .value = TextureType::Cubemap, .name = "cubemap" },
        { .value = TextureType::EnvironmentMask, .name = "environment mask" },
        { .value = TextureType::ComplexMaterial, .name = "complex material" },
        { .value = TextureType::RMAOS, .name = "rmaos" },
        { .value = TextureType::SubsurfaceTint, .name = "subsurface tint" },
        { .value = TextureType::InnerLayer, .name = "inner layer" },
        { .value = TextureType::FuzzPBR, .name = "fuzz pbr" },
        { .value = TextureType::CoatNormalRoughness, .name = "coat normal roughness" },
        { .value = TextureType::Backlight, .name = "backlight" },
        { .value = TextureType::Specular, .name = "specular" },
        { .value = TextureType::HairFlowMap, .name = "hair flowmap" },
        { .value = TextureType::SubsurfacePBR, .name = "subsurface pbr" },
        { .value = TextureType::Unknown, .name = "unknown" },
    },
};

/**
 * @brief Converts a TextureType enum value to its string representation.
 *
 * @param type The texture type to convert.
 * @return String name of the texture type, or "unknown" if not found.
 */
std::string strFromTexType(const TextureType& type);

/**
 * @brief Converts a string name to the corresponding TextureType enum value.
 *
 * @param type String name of the texture type.
 * @return Corresponding TextureType, or TextureType::Unknown if not found.
 */
TextureType texTypeFromStr(const std::string& type);

/**
 * @brief Returns a list of all known texture type name strings.
 *
 * @return Vector of strings, one per TextureType enum value.
 */
std::vector<std::string> texTypesStr();

/// @brief Flags describing sub-channel properties within a Complex Material texture.
enum class TextureAttribute : uint8_t { CMEnvMask, CMGlossiness, CMMetalness, CMHeight };

}
