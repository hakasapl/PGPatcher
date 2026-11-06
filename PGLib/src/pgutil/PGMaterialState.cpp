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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <optional>
#include <span>
#include <string>
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

/// @brief One field of a BSLightingShaderProperty.
struct ShaderField {
    const char* key;
    std::function<nlohmann::json(const nifly::BSLightingShaderProperty&)> read;
    /// Applies a JSON value and tells whether the shader changed; a malformed value is ignored.
    std::function<bool(nifly::BSLightingShaderProperty&, const nlohmann::json&)> write;
    std::function<bool(const nifly::BSLightingShaderProperty&, const nifly::BSLightingShaderProperty&)> isEqual;
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
                       ShaderFloatMember member)
{
    return {
        .key = key,
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
                          ShaderUnsignedMember member)
{
    return {
        .key = key,
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

ShaderField textureClampModeField()
{
    return {
        .key = "texture_clamp_mode",
        .read =
            [](const nifly::BSLightingShaderProperty& shader) {
                return nlohmann::json(static_cast<uint32_t>(shader.textureClampMode));
            },
        .write =
            [](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                uint32_t newValue = 0;
                if (!PGMaterialState::unsignedFromJSON(value, newValue)
                    || static_cast<uint32_t>(shader.textureClampMode) == newValue) {
                    return false;
                }

                shader.textureClampMode = static_cast<nifly::TexClampMode>(newValue);
                return true;
            },
        .isEqual = [](const nifly::BSLightingShaderProperty& a,
                      const nifly::BSLightingShaderProperty& b) { return a.textureClampMode == b.textureClampMode; },
    };
}

ShaderField vector2Field(const char* key,
                         ShaderVector2Member member)
{
    return {
        .key = key,
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
                         ShaderVector3Member member)
{
    return {
        .key = key,
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
                        ShaderColor4Member member)
{
    return {
        .key = key,
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

/// @brief Number of parameters in a True PBR payload.
constexpr size_t numTruePBRParameters = 25;

/// @brief The parameters of a True PBR payload, in the order of the mesh format.
std::array<float,
           numTruePBRParameters>
truePBRParameters(const nifly::TruePBRData& pbr)
{
    return {
        pbr.roughnessScale,
        pbr.specularLevel,
        pbr.displacementScale,
        pbr.subsurfaceColor.x,
        pbr.subsurfaceColor.y,
        pbr.subsurfaceColor.z,
        pbr.subsurfaceOpacity,
        pbr.coatColor.x,
        pbr.coatColor.y,
        pbr.coatColor.z,
        pbr.coatStrength,
        pbr.coatRoughness,
        pbr.coatSpecularLevel,
        pbr.fuzzColor.x,
        pbr.fuzzColor.y,
        pbr.fuzzColor.z,
        pbr.fuzzWeight,
        pbr.glintScreenSpaceScale,
        pbr.glintLogMicrofacetDensity,
        pbr.glintMicrofacetRoughness,
        pbr.glintDensityRandomization,
        pbr.subsurfaceBalance,
        pbr.thinSurfaceFrontSpecularWeight,
        pbr.thinSurfaceBackSpecularWeight,
        pbr.thinSurfaceBackRoughnessScale,
    };
}

void setTruePBRParameters(nifly::TruePBRData& pbr,
                          const std::array<float,
                                           numTruePBRParameters>& parameters)
{
    pbr.roughnessScale = parameters.at(0);
    pbr.specularLevel = parameters.at(1);
    pbr.displacementScale = parameters.at(2);
    pbr.subsurfaceColor = nifly::Vector3(parameters.at(3), parameters.at(4), parameters.at(5));
    pbr.subsurfaceOpacity = parameters.at(6);
    pbr.coatColor = nifly::Vector3(parameters.at(7), parameters.at(8), parameters.at(9));
    pbr.coatStrength = parameters.at(10);
    pbr.coatRoughness = parameters.at(11);
    pbr.coatSpecularLevel = parameters.at(12);
    pbr.fuzzColor = nifly::Vector3(parameters.at(13), parameters.at(14), parameters.at(15));
    pbr.fuzzWeight = parameters.at(16);
    pbr.glintScreenSpaceScale = parameters.at(17);
    pbr.glintLogMicrofacetDensity = parameters.at(18);
    pbr.glintMicrofacetRoughness = parameters.at(19);
    pbr.glintDensityRandomization = parameters.at(20);
    pbr.subsurfaceBalance = parameters.at(21);
    pbr.thinSurfaceFrontSpecularWeight = parameters.at(22);
    pbr.thinSurfaceBackSpecularWeight = parameters.at(23);
    pbr.thinSurfaceBackRoughnessScale = parameters.at(24);
}

/// @brief The True PBR payload, which a mesh only stores for the standalone True PBR shader type. It reads as null for
/// any other shader, which leaves the key out of the JSON.
ShaderField truePBRField()
{
    return {
        .key = "true_pbr",
        .read =
            [](const nifly::BSLightingShaderProperty& shader) {
                if (shader.GetShaderType() != nifly::BSLSP_TRUEPBR)
                    return nlohmann::json();

                nlohmann::json parameters = nlohmann::json::array();
                for (const auto parameter : truePBRParameters(shader.truePBR))
                    parameters.push_back(jsonFloat(parameter));

                nlohmann::json out = nlohmann::json::object();
                out["features"] = shader.truePBR.features;
                out["parameters"] = parameters;
                return out;
            },
        .write =
            [](nifly::BSLightingShaderProperty& shader, const nlohmann::json& value) {
                if (!value.is_object() || !value.contains("features") || !value.contains("parameters"))
                    return false;

                uint32_t newFeatures = 0;
                std::array<float, numTruePBRParameters> newParameters { };
                if (!PGMaterialState::unsignedFromJSON(value["features"], newFeatures)
                    || !floatsFromJSON(value["parameters"], newParameters)) {
                    return false;
                }

                if (shader.truePBR.features == newFeatures && truePBRParameters(shader.truePBR) == newParameters)
                    return false;

                shader.truePBR.features = newFeatures;
                setTruePBRParameters(shader.truePBR, newParameters);
                return true;
            },
        .isEqual =
            [](const nifly::BSLightingShaderProperty& a, const nifly::BSLightingShaderProperty& b) {
                if (a.GetShaderType() != nifly::BSLSP_TRUEPBR || b.GetShaderType() != nifly::BSLSP_TRUEPBR)
                    return true;

                return a.truePBR.features == b.truePBR.features
                    && truePBRParameters(a.truePBR) == truePBRParameters(b.truePBR);
            },
    };
}

/// @brief Every field the SSE mesh format stores for a BSLightingShaderProperty, including the BSShaderProperty ones.
/// The fields of later formats (the Fallout 4 wetness values, the Fallout 76 subsurface color and so on) are never
/// read from or written to a Skyrim SE mesh, so they are left out on purpose. The "subsurface_color" of a PBR JSON is
/// not that field: True PBR repurposes the specular color for it, which is covered below.
const std::vector<ShaderField>& shaderFields()
{
    static const std::vector<ShaderField> fields = {
        shaderTypeField(),
        unsignedField("shader_flags_1", &nifly::BSLightingShaderProperty::shaderFlags1),
        unsignedField("shader_flags_2", &nifly::BSLightingShaderProperty::shaderFlags2),
        vector2Field("uv_offset", &nifly::BSLightingShaderProperty::uvOffset),
        vector2Field("uv_scale", &nifly::BSLightingShaderProperty::uvScale),
        vector3Field("emissive_color", &nifly::BSLightingShaderProperty::emissiveColor),
        floatField("emissive_multiple", &nifly::BSLightingShaderProperty::emissiveMultiple),
        textureClampModeField(),
        floatField("alpha", &nifly::BSLightingShaderProperty::alpha),
        floatField("refraction_strength", &nifly::BSLightingShaderProperty::refractionStrength),
        floatField("glossiness", &nifly::BSLightingShaderProperty::glossiness),
        vector3Field("specular_color", &nifly::BSLightingShaderProperty::specularColor),
        floatField("specular_strength", &nifly::BSLightingShaderProperty::specularStrength),
        floatField("lighting_effect_1", &nifly::BSLightingShaderProperty::softlighting),
        floatField("lighting_effect_2", &nifly::BSLightingShaderProperty::rimlightPower),
        floatField("environment_map_scale", &nifly::BSLightingShaderProperty::environmentMapScale),
        vector3Field("skin_tint_color", &nifly::BSLightingShaderProperty::skinTintColor),
        vector3Field("hair_tint_color", &nifly::BSLightingShaderProperty::hairTintColor),
        floatField("max_passes", &nifly::BSLightingShaderProperty::maxPasses),
        floatField("scale", &nifly::BSLightingShaderProperty::scale),
        floatField("parallax_inner_layer_thickness", &nifly::BSLightingShaderProperty::parallaxInnerLayerThickness),
        floatField("parallax_refraction_scale", &nifly::BSLightingShaderProperty::parallaxRefractionScale),
        vector2Field("parallax_inner_layer_texture_scale",
                     &nifly::BSLightingShaderProperty::parallaxInnerLayerTextureScale),
        floatField("parallax_envmap_strength", &nifly::BSLightingShaderProperty::parallaxEnvmapStrength),
        color4Field("sparkle_parameters", &nifly::BSLightingShaderProperty::sparkleParameters),
        floatField("eye_cubemap_scale", &nifly::BSLightingShaderProperty::eyeCubemapScale),
        vector3Field("eye_left_reflection_center", &nifly::BSLightingShaderProperty::eyeLeftReflectionCenter),
        vector3Field("eye_right_reflection_center", &nifly::BSLightingShaderProperty::eyeRightReflectionCenter),
        truePBRField(),
    };

    return fields;
}

} // namespace

nlohmann::json PGMaterialState::shaderToJSON(const nifly::BSLightingShaderProperty& shader)
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& field : shaderFields()) {
        auto value = field.read(shader);
        if (!value.is_null())
            out[field.key] = std::move(value);
    }

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
