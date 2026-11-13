#include "pgutil/PGShapeChanges.hpp"

#include "NIFBuilder.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>

namespace {

using TestUtil::NIFBuilder;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal,
                          const std::wstring& glow = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    set[2] = glow;
    return set;
}

TEST(PGShapeChangesTest,
     AShapeThePatchLeftAloneHasNoChanges)
{
    NIFBuilder builder;
    const auto textures = slots(L"textures\\a.dds", L"textures\\a_n.dds");
    auto* const shape = builder.addShape("Shape", textures);

    const auto state = PGShapeChanges::stateOf(builder.nif(), *shape, textures);
    ASSERT_TRUE(state.shader);
    EXPECT_EQ(state.slots, textures);
    EXPECT_TRUE(PGShapeChanges::changes(state, state).empty());

    // A shape without a lighting shader has no shader to compare.
    auto* const effect = builder.addEffectShape("Glow", "textures\\fx\\glow.dds");
    EXPECT_FALSE(PGShapeChanges::stateOf(builder.nif(), *effect, textures).shader);
}

TEST(PGShapeChangesTest,
     ChangesListTheShaderAndTexturesUnderNiflyNames)
{
    NIFBuilder builder;
    const auto& nif = builder.nif();
    auto* const shape = builder.addShape("Shape");
    auto* const shader = builder.lightingShader(shape);
    ASSERT_NE(shader, nullptr);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    const auto original
        = PGShapeChanges::stateOf(nif, *shape, slots(L"textures\\a.dds", L"textures\\a_n.dds", L"textures\\a_g.dds"));

    // What a PBR patch does: the PBR flag on and environment mapping off, new shader values, and the textures of the
    // PBR texture set, one slot emptied. Its alpha property, vertex data and extra data are left out.
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_UNUSED01);
    PGNIFUtil::clearShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    shader->SetShaderType(nifly::BSLSP_MULTILAYERPARALLAX);
    shader->glossiness = 0.04F;
    shader->uvScale = nifly::Vector2(2, 2);
    builder.addAlphaProperty(shape, 4845, 64);
    builder.addStringExtraData(shape, "PG_STOCK", "{}");
    shape->SetVertexColors(true);
    auto patchedSlots = slots(L"textures\\pbr\\a.dds", L"textures\\pbr\\a_n.dds");
    patchedSlots[5] = L"textures\\pbr\\a_rmaos.dds";
    const auto patched = PGShapeChanges::stateOf(nif, *shape, patchedSlots);

    // The shader type and the flag words as the raw numbers the mesh stores.
    auto expected = nlohmann::json::parse(R"({
        "BSLightingShaderProperty": {"bslspShaderType": 11, "glossiness": 0.04, "uvScale": [2, 2]},
        "BSShaderTextureSet": {"0": "pbr\\a.dds", "1": "pbr\\a_n.dds", "2": "", "5": "pbr\\a_rmaos.dds"}})");
    expected["BSLightingShaderProperty"]["shaderFlags1"] = shader->shaderFlags1;
    expected["BSLightingShaderProperty"]["shaderFlags2"] = shader->shaderFlags2;
    const auto changes = PGShapeChanges::changes(original, patched);
    EXPECT_EQ(changes, expected);
    EXPECT_EQ(changes["BSLightingShaderProperty"]["glossiness"].dump(), "0.04");
}

TEST(PGShapeChangesTest,
     AValueWrittenOverTheSameValueIsNoChange)
{
    NIFBuilder builder;
    const auto& nif = builder.nif();
    auto* const shape = builder.addShape("Shape");
    auto* const shader = builder.lightingShader(shape);
    ASSERT_NE(shader, nullptr);
    shader->glossiness = 0.04F;
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_UNUSED01);
    const auto original
        = PGShapeChanges::stateOf(nif, *shape, slots(L"textures\\pbr\\a.dds", L"textures\\pbr\\a_n.dds"));

    // A PBR entry that asks for what the mesh already has: the same glossiness, the PBR flag it has, and the same
    // textures, one of them spelled without the textures folder and in other case. Only the specular strength, which
    // the entry does change, is listed.
    shader->glossiness = 0.04F;
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_UNUSED01);
    shader->specularStrength = 2;
    EXPECT_EQ(PGShapeChanges::changes(
                  original, PGShapeChanges::stateOf(nif, *shape, slots(L"PBR\\A.dds", L"textures\\pbr\\a_n.dds"))),
              nlohmann::json::parse(R"({"BSLightingShaderProperty": {"specularStrength": 2}})"));
}
}
