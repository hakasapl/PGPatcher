#include "pgutil/PGMaterialState.hpp"

#include "NIFBuilder.hpp"
#include "pgutil/PGTypes.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using TestUtil::NIFBuilder;

const std::vector<std::string> s_expectedKeys {
    "shader_type",
    "shader_flags_1",
    "shader_flags_2",
    "uv_offset",
    "uv_scale",
    "emissive_color",
    "emissive_multiple",
    "texture_clamp_mode",
    "alpha",
    "refraction_strength",
    "glossiness",
    "specular_color",
    "specular_strength",
    "lighting_effect_1",
    "lighting_effect_2",
    "environment_map_scale",
    "skin_tint_color",
    "hair_tint_color",
    "max_passes",
    "scale",
    "parallax_inner_layer_thickness",
    "parallax_refraction_scale",
    "parallax_inner_layer_texture_scale",
    "parallax_envmap_strength",
    "sparkle_parameters",
    "eye_cubemap_scale",
    "eye_left_reflection_center",
    "eye_right_reflection_center",
};

void setNonDefaultValues(nifly::BSLightingShaderProperty& shader)
{
    shader.SetShaderType(nifly::BSLSP_PARALLAX);
    shader.shaderFlags1 = 0x12345678U;
    shader.shaderFlags2 = 0x9ABCDEF0U;
    shader.uvOffset = nifly::Vector2(0.25F, 0.5F);
    shader.uvScale = nifly::Vector2(2, 3);
    shader.emissiveColor = nifly::Vector3(0.1F, 0.2F, 0.3F);
    shader.emissiveMultiple = 2.5F;
    shader.textureClampMode = 0;
    shader.alpha = 0.5F;
    shader.refractionStrength = 0.1F;
    shader.glossiness = 12.5F;
    shader.specularColor = nifly::Vector3(0.2F, 0.4F, 0.6F);
    shader.specularStrength = 3;
    shader.softlighting = 0.7F;
    shader.rimlightPower = 4;
    shader.environmentMapScale = 0.8F;
    shader.skinTintColor = nifly::Vector3(0.9F, 0.8F, 0.7F);
    shader.hairTintColor = nifly::Vector3(0.6F, 0.5F, 0.4F);
    shader.maxPasses = 2;
    shader.scale = 1.5F;
    shader.parallaxInnerLayerThickness = 0.3F;
    shader.parallaxRefractionScale = 0.9F;
    shader.parallaxInnerLayerTextureScale = nifly::Vector2(3, 4);
    shader.parallaxEnvmapStrength = 0.6F;
    shader.sparkleParameters = nifly::Color4(1, 2, 3, 4);
    shader.eyeCubemapScale = 1.2F;
    shader.eyeLeftReflectionCenter = nifly::Vector3(1, 2, 3);
    shader.eyeRightReflectionCenter = nifly::Vector3(4, 5, 6);
}

TEST(PGMaterialStateTest,
     ShaderToJsonListsEveryFieldWithItsDefault)
{
    NIFBuilder builder;
    auto* shader = builder.lightingShader(builder.addShape("Shape"));
    ASSERT_NE(shader, nullptr);

    const auto json = PGMaterialState::shaderToJSON(*shader);
    ASSERT_TRUE(json.is_object());
    EXPECT_EQ(json.size(), s_expectedKeys.size());
    for (const auto& key : s_expectedKeys)
        EXPECT_TRUE(json.contains(key)) << key;

    // nifly gives a Skyrim SE shader the game's defaults (glossiness 20 and a couple of flags), not the header's.
    EXPECT_EQ(json["shader_type"], 0);
    EXPECT_EQ(json["shader_flags_1"], shader->shaderFlags1);
    EXPECT_EQ(json["shader_flags_2"], shader->shaderFlags2);
    EXPECT_EQ(json["shader_flags_2"], 0x8001U);
    EXPECT_EQ(json["uv_offset"], nlohmann::json({ 0, 0 }));
    EXPECT_EQ(json["uv_scale"], nlohmann::json({ 1, 1 }));
    EXPECT_EQ(json["emissive_color"], nlohmann::json({ 0, 0, 0 }));
    EXPECT_EQ(json["emissive_multiple"], 1);
    EXPECT_EQ(json["texture_clamp_mode"], 3);
    EXPECT_EQ(json["alpha"], 1);
    EXPECT_EQ(json["refraction_strength"], 0);
    EXPECT_EQ(json["glossiness"], 20);
    EXPECT_EQ(json["specular_color"], nlohmann::json({ 1, 1, 1 }));
    EXPECT_EQ(json["specular_strength"], 1);
    EXPECT_EQ(json["lighting_effect_2"], 2);
    EXPECT_EQ(json["environment_map_scale"], 1);
    EXPECT_EQ(json["skin_tint_color"], nlohmann::json({ 1, 1, 1 }));
    EXPECT_EQ(json["hair_tint_color"], nlohmann::json({ 1, 1, 1 }));
    EXPECT_EQ(json["max_passes"], 1);
    EXPECT_EQ(json["scale"], 1);
    EXPECT_EQ(json["parallax_inner_layer_thickness"], 0);
    EXPECT_EQ(json["parallax_refraction_scale"], 1);
    EXPECT_EQ(json["parallax_inner_layer_texture_scale"], nlohmann::json({ 1, 1 }));
    EXPECT_EQ(json["parallax_envmap_strength"], 1);
    EXPECT_EQ(json["sparkle_parameters"], nlohmann::json({ 0, 0, 0, 0 }));
    EXPECT_EQ(json["eye_cubemap_scale"], 1);
    EXPECT_EQ(json["eye_left_reflection_center"], nlohmann::json({ 0, 0, 0 }));
    EXPECT_EQ(json["eye_right_reflection_center"], nlohmann::json({ 0, 0, 0 }));

    // Floats are written with nine significant digits, which reads back as the same float.
    EXPECT_EQ(static_cast<float>(json["lighting_effect_1"].get<double>()), 0.3F);
}

TEST(PGMaterialStateTest,
     ShaderFromJsonAppliesPresentFieldsAndReportsChanges)
{
    NIFBuilder builder;
    auto* shader = builder.lightingShader(builder.addShape("Shape"));
    ASSERT_NE(shader, nullptr);

    const nlohmann::json changes = {
        { "glossiness", 80 },        { "specular_color", { 0.5, 0.25, 1 } },
        { "shader_type", 1 },        { "shader_flags_2", 33 },
        { "uv_scale", { 2, 3 } },    { "sparkle_parameters", { 1, 2, 3, 4 } },
        { "texture_clamp_mode", 0 },
    };
    EXPECT_TRUE(PGMaterialState::shaderFromJSON(*shader, changes));
    EXPECT_EQ(shader->glossiness, 80);
    EXPECT_EQ(shader->specularColor, nifly::Vector3(0.5F, 0.25F, 1));
    EXPECT_EQ(shader->GetShaderType(), 1U);
    EXPECT_EQ(shader->shaderFlags2, 33U);
    EXPECT_EQ(shader->uvScale.u, 2);
    EXPECT_EQ(shader->uvScale.v, 3);
    EXPECT_EQ(shader->sparkleParameters.r, 1);
    EXPECT_EQ(shader->sparkleParameters.g, 2);
    EXPECT_EQ(shader->sparkleParameters.b, 3);
    EXPECT_EQ(shader->sparkleParameters.a, 4);
    EXPECT_EQ(shader->textureClampMode, 0U);

    // Fields that are not in the JSON keep their value.
    EXPECT_EQ(shader->alpha, 1);
    EXPECT_EQ(shader->shaderFlags1, builder.lightingShader(builder.addShape("Untouched"))->shaderFlags1);

    // Applying the same values again changes nothing.
    EXPECT_FALSE(PGMaterialState::shaderFromJSON(*shader, changes));
    EXPECT_FALSE(PGMaterialState::shaderFromJSON(*shader, nlohmann::json::object()));
    EXPECT_FALSE(PGMaterialState::shaderFromJSON(*shader, { { "not_a_field", 5 } }));
}

TEST(PGMaterialStateTest,
     ShaderFromJsonIgnoresMalformedValues)
{
    NIFBuilder builder;
    auto* shader = builder.lightingShader(builder.addShape("Shape"));
    auto* pristine = builder.lightingShader(builder.addShape("Pristine"));
    ASSERT_NE(shader, nullptr);
    ASSERT_NE(pristine, nullptr);

    const std::vector<nlohmann::json> malformed {
        { { "glossiness", "eighty" } },
        { { "glossiness", 1e39 } }, // not finite as a float
        { { "glossiness", nullptr } },
        { { "specular_color", { 1, 2 } } },
        { { "specular_color", "red" } },
        { { "specular_color", { 1, "x", 3 } } },
        { { "shader_flags_1", -1 } },
        { { "shader_flags_1", 4294967296 } },
        { { "shader_flags_1", 1.5 } },
        { { "shader_type", -2 } },
        { { "shader_type", "parallax" } },
        { { "uv_offset", { 1, 2, 3 } } },
        { { "sparkle_parameters", { 1, 2, 3 } } },
        { { "texture_clamp_mode", true } },
    };
    for (const auto& json : malformed) {
        EXPECT_FALSE(PGMaterialState::shaderFromJSON(*shader, json)) << json.dump();
        EXPECT_TRUE(PGMaterialState::isShaderEqual(*shader, *pristine)) << json.dump();
    }

    // A malformed value does not stop the valid ones from being applied.
    EXPECT_TRUE(PGMaterialState::shaderFromJSON(*shader, { { "glossiness", "bad" }, { "alpha", 0.5 } }));
    EXPECT_EQ(shader->alpha, 0.5F);
    EXPECT_EQ(shader->glossiness, pristine->glossiness);
}

TEST(PGMaterialStateTest,
     IsShaderEqualComparesEveryStoredField)
{
    NIFBuilder builder;
    auto* a = builder.lightingShader(builder.addShape("A"));
    auto* b = builder.lightingShader(builder.addShape("B"));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_TRUE(PGMaterialState::isShaderEqual(*a, *b));

    b->glossiness = 2;
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->glossiness = a->glossiness;
    EXPECT_TRUE(PGMaterialState::isShaderEqual(*a, *b));

    b->shaderFlags1 |= nifly::SLSF1_PARALLAX;
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->shaderFlags1 = a->shaderFlags1;

    b->SetShaderType(nifly::BSLSP_ENVMAP);
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->SetShaderType(nifly::BSLSP_DEFAULT);

    b->uvScale = nifly::Vector2(1, 2);
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->uvScale = a->uvScale;

    b->emissiveColor = nifly::Vector3(0, 0, 0.5F);
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->emissiveColor = a->emissiveColor;

    b->sparkleParameters = nifly::Color4(0, 0, 0, 1);
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->sparkleParameters = a->sparkleParameters;

    b->textureClampMode = 0;
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->textureClampMode = a->textureClampMode;

    b->eyeRightReflectionCenter = nifly::Vector3(1, 0, 0);
    EXPECT_FALSE(PGMaterialState::isShaderEqual(*a, *b));
    b->eyeRightReflectionCenter = a->eyeRightReflectionCenter;
    EXPECT_TRUE(PGMaterialState::isShaderEqual(*a, *b));

    // Fields of later formats are not part of a Skyrim SE mesh and are ignored on purpose.
    b->wetnessSpecScale = 5;
    b->backlightPower = 3;
    EXPECT_TRUE(PGMaterialState::isShaderEqual(*a, *b));
}

TEST(PGMaterialStateTest,
     ShaderJsonRoundTripRestoresEveryField)
{
    NIFBuilder builder;
    auto* source = builder.lightingShader(builder.addShape("Source"));
    auto* target = builder.lightingShader(builder.addShape("Target"));
    ASSERT_NE(source, nullptr);
    ASSERT_NE(target, nullptr);
    setNonDefaultValues(*source);
    ASSERT_FALSE(PGMaterialState::isShaderEqual(*source, *target));

    const auto json = PGMaterialState::shaderToJSON(*source);
    EXPECT_TRUE(PGMaterialState::shaderFromJSON(*target, json));
    EXPECT_TRUE(PGMaterialState::isShaderEqual(*source, *target));
    EXPECT_EQ(PGMaterialState::shaderToJSON(*target), json);
    EXPECT_EQ(target->GetShaderType(), nifly::BSLSP_PARALLAX);
    EXPECT_EQ(target->glossiness, 12.5F);
    EXPECT_EQ(target->softlighting, 0.7F);
    EXPECT_EQ(target->shaderFlags2, 0x9ABCDEF0U);
}

TEST(PGMaterialStateTest,
     TextureSlotsToJsonLowerCasesEverySlot)
{
    NIFBuilder builder;
    PGTypes::TextureSet textures;
    textures[0] = L"Textures\\A\\Stone.DDS";
    textures[1] = L"textures\\a\\stone_n.dds";
    textures[8] = L"textures\\a\\stone_x.dds";
    auto* shape = builder.addShape("Shape", textures);

    const auto json = PGMaterialState::textureSlotsToJSON(builder.nif(), *shape);
    ASSERT_TRUE(json.is_object());
    EXPECT_EQ(json.size(), numTextureSlots);
    EXPECT_EQ(json["0"], "textures\\a\\stone.dds");
    EXPECT_EQ(json["1"], "textures\\a\\stone_n.dds");
    EXPECT_EQ(json["2"], "");
    EXPECT_EQ(json["7"], "");
    EXPECT_EQ(json["8"], "textures\\a\\stone_x.dds");

    // An effect shader has no texture set, but nifly maps its source texture to the first slot.
    auto* effect = builder.addEffectShape("Effect", "Textures\\FX.dds");
    const auto effectJson = PGMaterialState::textureSlotsToJSON(builder.nif(), *effect);
    EXPECT_EQ(effectJson.size(), numTextureSlots);
    EXPECT_EQ(effectJson["0"], "textures\\fx.dds");
    for (unsigned slot = 1; slot < numTextureSlots; slot++)
        EXPECT_EQ(effectJson[std::to_string(slot)], "") << slot;

    // A shape without any shader gives only empty slots.
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    const auto shaderlessJson = PGMaterialState::textureSlotsToJSON(builder.nif(), *shaderless);
    EXPECT_EQ(shaderlessJson.size(), numTextureSlots);
    for (const auto& [key, value] : shaderlessJson.items())
        EXPECT_EQ(value, "") << key;
}

TEST(PGMaterialStateTest,
     IsTextureSetEqualIgnoresCaseAndTrailingEmptySlots)
{
    nifly::NiVersion version = nifly::NiVersion::getSSE();
    nifly::BSShaderTextureSet a(version);
    nifly::BSShaderTextureSet b(version);
    ASSERT_EQ(a.textures.size(), numTextureSlots);
    EXPECT_TRUE(PGMaterialState::isTextureSetEqual(a, b));

    a.textures[0].get() = "Textures\\A\\Stone.DDS";
    EXPECT_FALSE(PGMaterialState::isTextureSetEqual(a, b));
    b.textures[0].get() = "textures\\a\\stone.dds";
    EXPECT_TRUE(PGMaterialState::isTextureSetEqual(a, b));

    b.textures[1].get() = "textures\\a\\stone_n.dds";
    EXPECT_FALSE(PGMaterialState::isTextureSetEqual(a, b));
    EXPECT_FALSE(PGMaterialState::isTextureSetEqual(b, a));
    b.textures[1].get() = "";
    EXPECT_TRUE(PGMaterialState::isTextureSetEqual(a, b));

    // Extra slots only matter when they hold something.
    b.textures.resize(13);
    EXPECT_TRUE(PGMaterialState::isTextureSetEqual(a, b));
    EXPECT_TRUE(PGMaterialState::isTextureSetEqual(b, a));
    b.textures[12].get() = "textures\\a\\extra.dds";
    EXPECT_FALSE(PGMaterialState::isTextureSetEqual(a, b));
    EXPECT_FALSE(PGMaterialState::isTextureSetEqual(b, a));
}

TEST(PGMaterialStateTest,
     AlphaPropertyJsonRoundTripsAndNullStandsForNone)
{
    NIFBuilder builder;
    auto& nif = builder.nif();
    auto* shape = builder.addShape("Shape");
    EXPECT_TRUE(PGMaterialState::alphaPropertyToJSON(nif, *shape).is_null());

    // An object gives a shape without an alpha property one, with the defaults for the fields it leaves out.
    EXPECT_TRUE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, { { "threshold", 64 } }));
    auto* alpha = nif.GetAlphaProperty(shape);
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->flags, 4844U);
    EXPECT_EQ(alpha->threshold, 64U);
    EXPECT_EQ(PGMaterialState::alphaPropertyToJSON(nif, *shape),
              nlohmann::json({ { "flags", 4844 }, { "threshold", 64 } }));

    // Present fields are applied, and the same values change nothing.
    EXPECT_TRUE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, { { "flags", 4845 } }));
    EXPECT_EQ(alpha->flags, 4845U);
    EXPECT_EQ(alpha->threshold, 64U);
    EXPECT_FALSE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, { { "flags", 4845 }, { "threshold", 64 } }));
    EXPECT_FALSE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, nlohmann::json::object()));

    // Malformed values are ignored, and do not stop the valid ones from being applied.
    const std::vector<nlohmann::json> malformed {
        { { "flags", "opaque" } },
        { { "flags", 65536 } },
        { { "flags", -1 } },
        { { "flags", 1.5 } },
        { { "threshold", 256 } },
        { { "threshold", true } },
        "opaque",
        5,
    };
    for (const auto& json : malformed) {
        EXPECT_FALSE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, json)) << json.dump();
        EXPECT_EQ(alpha->flags, 4845U) << json.dump();
        EXPECT_EQ(alpha->threshold, 64U) << json.dump();
    }
    EXPECT_TRUE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, { { "flags", "bad" }, { "threshold", 32 } }));
    EXPECT_EQ(alpha->flags, 4845U);
    EXPECT_EQ(alpha->threshold, 32U);

    // Null removes the alpha property.
    EXPECT_TRUE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, nullptr));
    EXPECT_EQ(nif.GetAlphaProperty(shape), nullptr);
    EXPECT_FALSE(PGMaterialState::alphaPropertyFromJSON(nif, *shape, nullptr));
}

TEST(PGMaterialStateTest,
     IsAlphaPropertyEqualComparesBothFieldsAndExistence)
{
    const nifly::NiAlphaProperty a { };
    nifly::NiAlphaProperty b;
    EXPECT_TRUE(PGMaterialState::isAlphaPropertyEqual(&a, &b));
    EXPECT_TRUE(PGMaterialState::isAlphaPropertyEqual(nullptr, nullptr));
    EXPECT_FALSE(PGMaterialState::isAlphaPropertyEqual(&a, nullptr));
    EXPECT_FALSE(PGMaterialState::isAlphaPropertyEqual(nullptr, &b));

    b.flags = 4845;
    EXPECT_FALSE(PGMaterialState::isAlphaPropertyEqual(&a, &b));
    b.flags = a.flags;
    b.threshold = 0;
    EXPECT_FALSE(PGMaterialState::isAlphaPropertyEqual(&a, &b));
    b.threshold = a.threshold;
    EXPECT_TRUE(PGMaterialState::isAlphaPropertyEqual(&a, &b));
}

TEST(PGMaterialStateTest,
     UnsignedFromJsonChecksTheRangeOfTheType)
{
    uint8_t byte = 1;
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(255), byte));
    EXPECT_EQ(byte, 255U);
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(256), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(-1), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(1.5), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(-0.5), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(256.0), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json("2"), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(true), byte));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(nullptr), byte));
    EXPECT_EQ(byte, 255U); // untouched by the malformed values

    // A float without a fraction counts as its integer, as it does for an "integer" of a JSON schema.
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(2.0), byte));
    EXPECT_EQ(byte, 2U);
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json::parse("128.0"), byte));
    EXPECT_EQ(byte, 128U);
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(255.0), byte));
    EXPECT_EQ(byte, 255U);

    uint16_t word = 0;
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(65535), word));
    EXPECT_EQ(word, 65535U);
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(65536), word));
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json(65536.0), word));
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(65535.0), word));
    EXPECT_EQ(word, 65535U);

    // Parsed numbers are unsigned, numbers built from an int are signed; both are read.
    uint32_t dword = 0;
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json::parse("4294967295"), dword));
    EXPECT_EQ(dword, 4294967295U);
    EXPECT_FALSE(PGMaterialState::unsignedFromJSON(nlohmann::json::parse("4294967296"), dword));
    EXPECT_TRUE(PGMaterialState::unsignedFromJSON(nlohmann::json(7), dword));
    EXPECT_EQ(dword, 7U);
}
}
