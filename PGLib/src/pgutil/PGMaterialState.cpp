#include "pgutil/PGMaterialState.hpp"

#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

// Keys of the NiAlphaProperty fields, named after the fields NifSkope shows. PG_STOCK blocks store them, so they are
// part of that format and never change.
constexpr const char* keyAlphaFlags = "flags";
constexpr const char* keyAlphaThreshold = "threshold";

/// @brief Rounds a float to a double with at most nine significant digits, which reads back as the same float and
/// keeps the JSON short.
double jsonFloat(float value) { return std::strtod(fmt::format("{:.9g}", value).c_str(), nullptr); }

nlohmann::json jsonFromVector(const nifly::Vector2& value) { return { jsonFloat(value.u), jsonFloat(value.v) }; }

nlohmann::json jsonFromVector(const nifly::Vector3& value)
{
    return { jsonFloat(value.x), jsonFloat(value.y), jsonFloat(value.z) };
}

nlohmann::json jsonFromColor(const nifly::Color4& value)
{
    return { jsonFloat(value.r), jsonFloat(value.g), jsonFloat(value.b), jsonFloat(value.a) };
}

/// @brief Reads a JSON number as a float. A number that is not finite as a float is malformed.
bool floatFromJSON(const nlohmann::json& value,
                   float& out)
{
    if (!value.is_number())
        return false;

    const auto asFloat = static_cast<float>(value.get<double>());
    if (!std::isfinite(asFloat))
        return false;

    out = asFloat;
    return true;
}

/// @brief Reads an array of exactly N numbers.
template<size_t N>
bool floatsFromJSON(const nlohmann::json& value,
                    std::array<float,
                               N>& out)
{
    if (!value.is_array() || value.size() != N)
        return false;

    for (size_t i = 0; i < N; i++)
        if (!floatFromJSON(value.at(i), out.at(i)))
            return false;

    return true;
}

/// @brief Whether every number of a JSON value is finite: JSON has no number for the others.
bool isFiniteJSON(const nlohmann::json& value)
{
    if (value.is_number_float())
        return std::isfinite(value.get<double>());

    if (!value.is_array())
        return true;

    return std::ranges::all_of(value, [](const nlohmann::json& item) { return isFiniteJSON(item); });
}

/// @brief A JSON value as read() gives it with the shortest decimals that read back as the same floats.
nlohmann::json shortestJSON(const nlohmann::json& value)
{
    if (value.is_number_float())
        return PGMaterialState::shortestFloat(static_cast<float>(value.get<double>()));

    if (!value.is_array())
        return value;

    auto out = nlohmann::json::array();
    for (const auto& item : value)
        out.push_back(shortestJSON(item));

    return out;
}

/// @brief One field of a BSLightingShaderProperty.
struct ShaderField {
    /// The key of the stock marker format (PG_STOCK blocks), which never changes.
    const char* key;
    /// The nifly member the field is, which names it in shaderChanges().
    const char* niflyKey;
    std::function<nlohmann::json(const nifly::BSLightingShaderProperty&)> read;
    /// Applies a JSON value and tells whether the shader changed; a malformed value is ignored.
    std::function<bool(nifly::BSLightingShaderProperty&, const nlohmann::json&)> write;
    std::function<bool(const nifly::BSLightingShaderProperty&, const nifly::BSLightingShaderProperty&)> isEqual;
    /// The shader type the mesh stores the field for, as NifSkope shows it only for that type; nothing for a field
    /// every shader stores.
    std::optional<uint32_t> storingShaderType;
};

using ShaderFloatMember = float nifly::BSLightingShaderProperty::*;
using ShaderUnsignedMember = uint32_t nifly::BSLightingShaderProperty::*;
using ShaderVector2Member = nifly::Vector2 nifly::BSLightingShaderProperty::*;
using ShaderVector3Member = nifly::Vector3 nifly::BSLightingShaderProperty::*;
using ShaderColor4Member = nifly::Color4 nifly::BSLightingShaderProperty::*;

ShaderField shaderTypeField()
{
    return {
        .key = "shader_type",
        .niflyKey = "bslspShaderType",
        .read = [](const nifly::BSLightingShaderProperty& shader) { return nlohmann::json(shader.GetShaderType()); },
        .write =
            [](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                uint32_t newType = 0;
                if (!PGMaterialState::unsignedFromJSON(value, newType) || shader.GetShaderType() == newType)
                    return false;

                shader.SetShaderType(newType);
                return true;
            },
        .isEqual = [](const nifly::BSLightingShaderProperty& a,
                      const nifly::BSLightingShaderProperty& b) { return a.GetShaderType() == b.GetShaderType(); },
    };
}

ShaderField floatField(const char* key,
                       const char* niflyKey,
                       ShaderFloatMember member)
{
    return {
        .key = key,
        .niflyKey = niflyKey,
        .read
        = [member](const nifly::BSLightingShaderProperty& shader) { return nlohmann::json(jsonFloat(shader.*member)); },
        .write =
            [member](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                float newValue = 0;
                if (!floatFromJSON(value, newValue) || shader.*member == newValue)
                    return false;

                shader.*member = newValue;
                return true;
            },
        .isEqual = [member](const nifly::BSLightingShaderProperty& a,
                            const nifly::BSLightingShaderProperty& b) { return a.*member == b.*member; },
    };
}

ShaderField unsignedField(const char* key,
                          const char* niflyKey,
                          ShaderUnsignedMember member)
{
    return {
        .key = key,
        .niflyKey = niflyKey,
        .read = [member](const nifly::BSLightingShaderProperty& shader) { return nlohmann::json(shader.*member); },
        .write =
            [member](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                uint32_t newValue = 0;
                if (!PGMaterialState::unsignedFromJSON(value, newValue) || shader.*member == newValue)
                    return false;

                shader.*member = newValue;
                return true;
            },
        .isEqual = [member](const nifly::BSLightingShaderProperty& a,
                            const nifly::BSLightingShaderProperty& b) { return a.*member == b.*member; },
    };
}

ShaderField vector2Field(const char* key,
                         const char* niflyKey,
                         ShaderVector2Member member)
{
    return {
        .key = key,
        .niflyKey = niflyKey,
        .read = [member](const nifly::BSLightingShaderProperty& shader) { return jsonFromVector(shader.*member); },
        .write =
            [member](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                std::array<float, 2> components { };
                if (!floatsFromJSON(value, components))
                    return false;

                auto& field = shader.*member;
                if (field.u == components.at(0) && field.v == components.at(1))
                    return false;

                field = nifly::Vector2(components.at(0), components.at(1));
                return true;
            },
        .isEqual =
            [member](const nifly::BSLightingShaderProperty& a, const nifly::BSLightingShaderProperty& b) {
                return (a.*member).u == (b.*member).u && (a.*member).v == (b.*member).v;
            },
    };
}

ShaderField vector3Field(const char* key,
                         const char* niflyKey,
                         ShaderVector3Member member)
{
    return {
        .key = key,
        .niflyKey = niflyKey,
        .read = [member](const nifly::BSLightingShaderProperty& shader) { return jsonFromVector(shader.*member); },
        .write =
            [member](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                std::array<float, 3> components { };
                if (!floatsFromJSON(value, components))
                    return false;

                const nifly::Vector3 newValue(components.at(0), components.at(1), components.at(2));
                if (shader.*member == newValue)
                    return false;

                shader.*member = newValue;
                return true;
            },
        .isEqual = [member](const nifly::BSLightingShaderProperty& a,
                            const nifly::BSLightingShaderProperty& b) { return a.*member == b.*member; },
    };
}

ShaderField color4Field(const char* key,
                        const char* niflyKey,
                        ShaderColor4Member member)
{
    return {
        .key = key,
        .niflyKey = niflyKey,
        .read = [member](const nifly::BSLightingShaderProperty& shader) { return jsonFromColor(shader.*member); },
        .write =
            [member](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                std::array<float, 4> components { };
                if (!floatsFromJSON(value, components))
                    return false;

                auto& field = shader.*member;
                if (field.r == components.at(0) && field.g == components.at(1) && field.b == components.at(2)
                    && field.a == components.at(3)) {
                    return false;
                }

                field = nifly::Color4(components.at(0), components.at(1), components.at(2), components.at(3));
                return true;
            },
        .isEqual =
            [member](const nifly::BSLightingShaderProperty& a, const nifly::BSLightingShaderProperty& b) {
                const auto& fieldA = a.*member;
                const auto& fieldB = b.*member;
                return fieldA.r == fieldB.r && fieldA.g == fieldB.g && fieldA.b == fieldB.b && fieldA.a == fieldB.a;
            },
    };
}

/// @brief A field the mesh stores for one shader type only, see ShaderField::storingShaderType.
ShaderField storedFor(nifly::BSLightingShaderPropertyShaderType shaderType,
                      ShaderField field)
{
    field.storingShaderType = shaderType;
    return field;
}

/// @brief Every field the SSE mesh format stores for a BSLightingShaderProperty, including the BSShaderProperty ones.
/// The fields of later formats (the Fallout 4 wetness values, the Fallout 76 subsurface color and so on) are never
/// read from or written to a Skyrim SE mesh, so they are left out on purpose. The "subsurface_color" of a PBR JSON is
/// not that field: True PBR repurposes the specular color for it, which is covered below.
const std::vector<ShaderField>& shaderFields()
{
    static const std::vector<ShaderField> fields = {
        shaderTypeField(),
        unsignedField("shader_flags_1", "shaderFlags1", &nifly::BSLightingShaderProperty::shaderFlags1),
        unsignedField("shader_flags_2", "shaderFlags2", &nifly::BSLightingShaderProperty::shaderFlags2),
        vector2Field("uv_offset", "uvOffset", &nifly::BSLightingShaderProperty::uvOffset),
        vector2Field("uv_scale", "uvScale", &nifly::BSLightingShaderProperty::uvScale),
        vector3Field("emissive_color", "emissiveColor", &nifly::BSLightingShaderProperty::emissiveColor),
        floatField("emissive_multiple", "emissiveMultiple", &nifly::BSLightingShaderProperty::emissiveMultiple),
        unsignedField("texture_clamp_mode", "textureClampMode", &nifly::BSLightingShaderProperty::textureClampMode),
        floatField("alpha", "alpha", &nifly::BSLightingShaderProperty::alpha),
        floatField("refraction_strength", "refractionStrength", &nifly::BSLightingShaderProperty::refractionStrength),
        floatField("glossiness", "glossiness", &nifly::BSLightingShaderProperty::glossiness),
        vector3Field("specular_color", "specularColor", &nifly::BSLightingShaderProperty::specularColor),
        floatField("specular_strength", "specularStrength", &nifly::BSLightingShaderProperty::specularStrength),
        floatField("lighting_effect_1", "softlighting", &nifly::BSLightingShaderProperty::softlighting),
        floatField("lighting_effect_2", "rimlightPower", &nifly::BSLightingShaderProperty::rimlightPower),
        storedFor(nifly::BSLSP_ENVMAP,
                  floatField("environment_map_scale",
                             "environmentMapScale",
                             &nifly::BSLightingShaderProperty::environmentMapScale)),
        storedFor(nifly::BSLSP_SKINTINT,
                  vector3Field("skin_tint_color", "skinTintColor", &nifly::BSLightingShaderProperty::skinTintColor)),
        storedFor(nifly::BSLSP_HAIRTINT,
                  vector3Field("hair_tint_color", "hairTintColor", &nifly::BSLightingShaderProperty::hairTintColor)),
        storedFor(nifly::BSLSP_PARALLAXOCC,
                  floatField("max_passes", "maxPasses", &nifly::BSLightingShaderProperty::maxPasses)),
        storedFor(nifly::BSLSP_PARALLAXOCC, floatField("scale", "scale", &nifly::BSLightingShaderProperty::scale)),
        storedFor(nifly::BSLSP_MULTILAYERPARALLAX,
                  floatField("parallax_inner_layer_thickness",
                             "parallaxInnerLayerThickness",
                             &nifly::BSLightingShaderProperty::parallaxInnerLayerThickness)),
        storedFor(nifly::BSLSP_MULTILAYERPARALLAX,
                  floatField("parallax_refraction_scale",
                             "parallaxRefractionScale",
                             &nifly::BSLightingShaderProperty::parallaxRefractionScale)),
        storedFor(nifly::BSLSP_MULTILAYERPARALLAX,
                  vector2Field("parallax_inner_layer_texture_scale",
                               "parallaxInnerLayerTextureScale",
                               &nifly::BSLightingShaderProperty::parallaxInnerLayerTextureScale)),
        storedFor(nifly::BSLSP_MULTILAYERPARALLAX,
                  floatField("parallax_envmap_strength",
                             "parallaxEnvmapStrength",
                             &nifly::BSLightingShaderProperty::parallaxEnvmapStrength)),
        storedFor(nifly::BSLSP_MULTIINDEXSNOW,
                  color4Field(
                      "sparkle_parameters", "sparkleParameters", &nifly::BSLightingShaderProperty::sparkleParameters)),
        storedFor(
            nifly::BSLSP_EYE,
            floatField("eye_cubemap_scale", "eyeCubemapScale", &nifly::BSLightingShaderProperty::eyeCubemapScale)),
        storedFor(nifly::BSLSP_EYE,
                  vector3Field("eye_left_reflection_center",
                               "eyeLeftReflectionCenter",
                               &nifly::BSLightingShaderProperty::eyeLeftReflectionCenter)),
        storedFor(nifly::BSLSP_EYE,
                  vector3Field("eye_right_reflection_center",
                               "eyeRightReflectionCenter",
                               &nifly::BSLightingShaderProperty::eyeRightReflectionCenter)),
    };

    return fields;
}

} // namespace

nlohmann::json PGMaterialState::shaderToJSON(const nifly::BSLightingShaderProperty& shader)
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& field : shaderFields())
        out[field.key] = field.read(shader);

    return out;
}

bool PGMaterialState::shaderFromJSON(nifly::BSLightingShaderProperty& shader,
                                     const nlohmann::json& fields)
{
    bool isChanged = false;
    for (const auto& field : shaderFields()) {
        const auto it = fields.find(field.key);
        if (it != fields.end())
            isChanged |= field.write(shader, *it);
    }

    return isChanged;
}

nlohmann::json PGMaterialState::shaderChanges(const nifly::BSLightingShaderProperty& base,
                                              const nifly::BSLightingShaderProperty& patched)
{
    nlohmann::json changes = nlohmann::json::object();
    for (const auto& field : shaderFields()) {
        // A field the patched shader type does not store is no part of the shader as the mesh has it.
        if (field.storingShaderType && *field.storingShaderType != patched.GetShaderType())
            continue;

        if (field.isEqual(base, patched))
            continue;

        // A value JSON cannot write is left out; a field that was not finite before compares unequal to itself.
        auto value = shortestJSON(field.read(patched));
        if (isFiniteJSON(value))
            changes[field.niflyKey] = std::move(value);
    }

    return changes;
}

double PGMaterialState::shortestFloat(float value)
{
    std::array<char, 32> buffer { };
    auto number = static_cast<double>(value);
    const auto [end, ec] = std::to_chars(buffer.data(), std::to_address(buffer.end()), value);
    if (ec == std::errc())
        std::ignore = std::from_chars(buffer.data(), end, number);

    return number;
}

bool PGMaterialState::isShaderEqual(const nifly::BSLightingShaderProperty& shaderA,
                                    const nifly::BSLightingShaderProperty& shaderB)
{
    return std::ranges::all_of(shaderFields(),
                               [&](const ShaderField& field) { return field.isEqual(shaderA, shaderB); });
}

nlohmann::json PGMaterialState::textureSlotsToJSON(const nifly::NifFile& nif,
                                                   nifly::NiShape& nifShape)
{
    const auto slots = PGNIFUtil::textureSlots(&nif, &nifShape);

    nlohmann::json out = nlohmann::json::object();
    for (unsigned slot = 0; slot < numTextureSlots; slot++)
        out[std::to_string(slot)] = StringUtil::utf16toUTF8(slots.at(slot));

    return out;
}

bool PGMaterialState::isTextureSetEqual(const nifly::BSShaderTextureSet& texSetA,
                                        const nifly::BSShaderTextureSet& texSetB)
{
    // The slot vectors only expose const access through data(), so view them instead of copying them.
    const std::span<const nifly::NiString> texturesA(texSetA.textures.data(), texSetA.textures.size());
    const std::span<const nifly::NiString> texturesB(texSetB.textures.data(), texSetB.textures.size());
    const auto maxSize = std::max(texturesA.size(), texturesB.size());

    for (size_t i = 0; i < maxSize; i++) {
        const bool hasA = i < texturesA.size();
        const bool hasB = i < texturesB.size();

        if (hasA && hasB) {
            if (!StringUtil::asciiFastIEquals(texturesA[i].get(), texturesB[i].get()))
                return false;
        } else if (hasA) {
            if (!texturesA[i].get().empty())
                return false;
        } else { // hasB
            if (!texturesB[i].get().empty())
                return false;
        }
    }

    return true;
}

nlohmann::json PGMaterialState::alphaPropertyToJSON(const nifly::NifFile& nif,
                                                    nifly::NiShape& nifShape)
{
    const auto* const alphaProperty = nif.GetAlphaProperty(&nifShape);
    if (!alphaProperty)
        return nullptr;

    nlohmann::json out = nlohmann::json::object();
    out[keyAlphaFlags] = alphaProperty->flags;
    out[keyAlphaThreshold] = alphaProperty->threshold;

    return out;
}

bool PGMaterialState::alphaPropertyFromJSON(nifly::NifFile& nif,
                                            nifly::NiShape& nifShape,
                                            const nlohmann::json& state)
{
    if (state.is_null())
        return PGNIFUtil::deleteAlphaProperty(nif, nifShape);

    if (!state.is_object())
        return false;

    std::optional<uint16_t> flags;
    const auto flagsIt = state.find(keyAlphaFlags);
    uint16_t newFlags = 0;
    if (flagsIt != state.end() && unsignedFromJSON(*flagsIt, newFlags))
        flags = newFlags;

    std::optional<uint8_t> threshold;
    const auto thresholdIt = state.find(keyAlphaThreshold);
    uint8_t newThreshold = 0;
    if (thresholdIt != state.end() && unsignedFromJSON(*thresholdIt, newThreshold))
        threshold = newThreshold;

    // An object stands for an existing alpha property, so a shape without one gets it back.
    return PGNIFUtil::setAlphaProperty(nif, nifShape, flags, threshold, true);
}

bool PGMaterialState::isAlphaPropertyEqual(const nifly::NiAlphaProperty* alphaA,
                                           const nifly::NiAlphaProperty* alphaB)
{
    if (!alphaA || !alphaB)
        return alphaA == nullptr && alphaB == nullptr;

    return alphaA->flags == alphaB->flags && alphaA->threshold == alphaB->threshold;
}
