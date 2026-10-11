#include "pgutil/PGNIFUtil.hpp"

#include "NIFBuilder.hpp"
#include "TestUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using PGEnums::TextureSlots;
using PGEnums::TextureType;
using TestUtil::NIFBuilder;

PGTypes::TextureSet stoneTextures()
{
    PGTypes::TextureSet textures;
    textures[0] = L"textures\\a\\stone.dds";
    textures[1] = L"textures\\a\\stone_n.dds";
    textures[3] = L"textures\\a\\stone_p.dds";
    textures[5] = L"textures\\a\\stone_m.dds";
    return textures;
}

TEST(PGNIFUtilTest,
     SlotFromTexTypeMapsEveryType)
{
    const std::vector<std::pair<TextureType, TextureSlots>> expected {
        { TextureType::Diffuse, TextureSlots::Diffuse },
        { TextureType::Normal, TextureSlots::Normal },
        { TextureType::ModelSpaceNormal, TextureSlots::Normal },
        { TextureType::Emissive, TextureSlots::Glow },
        { TextureType::SkinTint, TextureSlots::Glow },
        { TextureType::SubsurfaceColor, TextureSlots::Glow },
        { TextureType::Height, TextureSlots::Parallax },
        { TextureType::HeightPBR, TextureSlots::Parallax },
        { TextureType::Cubemap, TextureSlots::Cubemap },
        { TextureType::EnvironmentMask, TextureSlots::EnvMask },
        { TextureType::ComplexMaterial, TextureSlots::EnvMask },
        { TextureType::RMAOS, TextureSlots::EnvMask },
        { TextureType::SubsurfaceTint, TextureSlots::MultiLayer },
        { TextureType::InnerLayer, TextureSlots::MultiLayer },
        { TextureType::FuzzPBR, TextureSlots::MultiLayer },
        { TextureType::CoatNormalRoughness, TextureSlots::MultiLayer },
        { TextureType::Backlight, TextureSlots::Backlight },
        { TextureType::Specular, TextureSlots::Backlight },
        { TextureType::HairFlowMap, TextureSlots::Backlight },
        { TextureType::SubsurfacePBR, TextureSlots::Backlight },
        { TextureType::Unknown, TextureSlots::Unknown },
    };
    ASSERT_EQ(expected.size(), static_cast<size_t>(TextureType::Unknown) + 1);
    for (const auto& [type, slot] : expected)
        EXPECT_EQ(PGNIFUtil::slotFromTexType(type), slot) << PGEnums::strFromTexType(type);

    EXPECT_EQ(PGNIFUtil::slotFromTexType(static_cast<TextureType>(200)), TextureSlots::Unknown);
}

TEST(PGNIFUtilTest,
     TexSuffixMapHoldsTheKnownSuffixes)
{
    const auto& map = PGNIFUtil::texSuffixMap();
    EXPECT_EQ(map.size(), 19U);

    const std::vector<std::tuple<std::wstring, TextureSlots, TextureType>> expected {
        { L"_bl", TextureSlots::Backlight, TextureType::Backlight },
        { L"_b", TextureSlots::Backlight, TextureType::Backlight },
        { L"_flow", TextureSlots::Backlight, TextureType::HairFlowMap },
        { L"_cnr", TextureSlots::MultiLayer, TextureType::CoatNormalRoughness },
        { L"_s", TextureSlots::MultiLayer, TextureType::SubsurfaceTint },
        { L"_i", TextureSlots::MultiLayer, TextureType::InnerLayer },
        { L"_f", TextureSlots::MultiLayer, TextureType::FuzzPBR },
        { L"_rmaos", TextureSlots::EnvMask, TextureType::RMAOS },
        { L"_envmask", TextureSlots::EnvMask, TextureType::EnvironmentMask },
        { L"_em", TextureSlots::EnvMask, TextureType::EnvironmentMask },
        { L"_m", TextureSlots::EnvMask, TextureType::EnvironmentMask },
        { L"_e", TextureSlots::Cubemap, TextureType::Cubemap },
        { L"_p", TextureSlots::Parallax, TextureType::Height },
        { L"_sk", TextureSlots::Glow, TextureType::SkinTint },
        { L"_g", TextureSlots::Glow, TextureType::Emissive },
        { L"_msn", TextureSlots::Normal, TextureType::Normal },
        { L"_n", TextureSlots::Normal, TextureType::Normal },
        { L"_d", TextureSlots::Diffuse, TextureType::Diffuse },
        { L"mask", TextureSlots::Diffuse, TextureType::Diffuse },
    };
    for (const auto& [suffix, slot, type] : expected) {
        ASSERT_TRUE(map.contains(suffix)) << StringUtil::utf16toUTF8(suffix);
        EXPECT_EQ(std::get<0>(map.at(suffix)), slot) << StringUtil::utf16toUTF8(suffix);
        EXPECT_EQ(std::get<1>(map.at(suffix)), type) << StringUtil::utf16toUTF8(suffix);
    }
}

TEST(PGNIFUtilTest,
     DefaultsFromSuffixReadsTheFileNameSuffix)
{
    using Result = std::tuple<TextureSlots, TextureType>;
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_n.dds"),
              Result(TextureSlots::Normal, TextureType::Normal));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_msn.dds"),
              Result(TextureSlots::Normal, TextureType::Normal));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"Textures\\A\\Stone_P.DDS"),
              Result(TextureSlots::Parallax, TextureType::Height));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\pbr\\a\\stone_p.dds"),
              Result(TextureSlots::Parallax, TextureType::HeightPBR));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_m.dds"),
              Result(TextureSlots::EnvMask, TextureType::EnvironmentMask));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_em.dds"),
              Result(TextureSlots::EnvMask, TextureType::EnvironmentMask));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_envmask.dds"),
              Result(TextureSlots::EnvMask, TextureType::EnvironmentMask));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_rmaos.dds"),
              Result(TextureSlots::EnvMask, TextureType::RMAOS));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_e.dds"),
              Result(TextureSlots::Cubemap, TextureType::Cubemap));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_g.dds"),
              Result(TextureSlots::Glow, TextureType::Emissive));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_sk.dds"),
              Result(TextureSlots::Glow, TextureType::SkinTint));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_s.dds"),
              Result(TextureSlots::MultiLayer, TextureType::SubsurfaceTint));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_bl.dds"),
              Result(TextureSlots::Backlight, TextureType::Backlight));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_flow.dds"),
              Result(TextureSlots::Backlight, TextureType::HairFlowMap));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_d.dds"),
              Result(TextureSlots::Diffuse, TextureType::Diffuse));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stonemask.dds"),
              Result(TextureSlots::Diffuse, TextureType::Diffuse));

    // No known suffix.
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone.dds"),
              Result(TextureSlots::Unknown, TextureType::Unknown));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L"textures\\a\\stone_x.dds"),
              Result(TextureSlots::Unknown, TextureType::Unknown));
    EXPECT_EQ(PGNIFUtil::defaultsFromSuffix(L""), Result(TextureSlots::Unknown, TextureType::Unknown));
}

TEST(PGNIFUtilTest,
     TexBaseStripsExtensionAndSlotSuffix)
{
    EXPECT_EQ(PGNIFUtil::texBase(L"Textures\\A\\Stone_N.dds", TextureSlots::Normal), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_msn.dds", TextureSlots::Normal), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_p.dds", TextureSlots::Parallax), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_m.dds", TextureSlots::EnvMask), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_em.dds", TextureSlots::EnvMask), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_rmaos.dds", TextureSlots::EnvMask), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_d.dds", TextureSlots::Diffuse), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stonemask.dds", TextureSlots::Diffuse), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_flow.dds", TextureSlots::Backlight), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_g.dds", TextureSlots::Glow), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_e.dds", TextureSlots::Cubemap), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_s.dds", TextureSlots::MultiLayer), L"textures\\a\\stone");

    // The suffix is only removed when it belongs to the slot.
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_n.dds", TextureSlots::Diffuse), L"textures\\a\\stone_n");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone_p.dds", TextureSlots::Normal), L"textures\\a\\stone_p");

    // Without a slot only the extension goes.
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\Stone_N.dds"), L"textures\\a\\stone_n");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone.dds", TextureSlots::Normal), L"textures\\a\\stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"stone.dds", TextureSlots::Unknown), L"stone");
    EXPECT_EQ(PGNIFUtil::texBase(L"textures\\a\\stone"), L"textures\\a\\stone");
}

TEST(PGNIFUtilTest,
     SearchPrefixesForTextureSets)
{
    const auto textures = stoneTextures();

    const auto bases = PGNIFUtil::searchPrefixes(textures);
    EXPECT_EQ(bases[0], L"textures\\a\\stone");
    EXPECT_EQ(bases[1], L"textures\\a\\stone");
    EXPECT_EQ(bases[2], L"");
    EXPECT_EQ(bases[3], L"textures\\a\\stone");
    EXPECT_EQ(bases[5], L"textures\\a\\stone");
    EXPECT_EQ(bases[8], L"");

    const auto full = PGNIFUtil::searchPrefixes(textures, false);
    EXPECT_EQ(full[0], L"textures\\a\\stone");
    EXPECT_EQ(full[1], L"textures\\a\\stone_n");
    EXPECT_EQ(full[3], L"textures\\a\\stone_p");
    EXPECT_EQ(full[5], L"textures\\a\\stone_m");

    const PGTypes::TextureSet empty;
    for (const auto& slot : PGNIFUtil::searchPrefixes(empty))
        EXPECT_TRUE(slot.empty());
}

TEST(PGNIFUtilTest,
     SearchPrefixesForShapes)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Stone", stoneTextures());

    const auto bases = PGNIFUtil::searchPrefixes(builder.nif(), shape);
    EXPECT_EQ(bases[0], L"textures\\a\\stone");
    EXPECT_EQ(bases[1], L"textures\\a\\stone");
    EXPECT_EQ(bases[2], L"");
    EXPECT_EQ(bases[3], L"textures\\a\\stone");
    EXPECT_EQ(bases[5], L"textures\\a\\stone");

    const auto full = PGNIFUtil::searchPrefixes(builder.nif(), shape, false);
    EXPECT_EQ(full[1], L"textures\\a\\stone_n");
    EXPECT_EQ(full[3], L"textures\\a\\stone_p");

    auto* bare = builder.addShape("Bare");
    for (const auto& slot : PGNIFUtil::searchPrefixes(builder.nif(), bare))
        EXPECT_TRUE(slot.empty());
}

TEST(PGNIFUtilTest,
     TexMatchFindsTexturesOfTheWantedTypeUnderABase)
{
    std::map<std::wstring, std::unordered_set<PGTypes::PGTexture, PGTypes::PGTextureHasher>> searchMap;
    searchMap[L"textures\\a\\stone"] = {
        { .path = L"textures\\a\\stone_n.dds", .type = TextureType::Normal },
        { .path = L"textures\\a\\stone_m.dds", .type = TextureType::EnvironmentMask },
        { .path = L"textures\\a\\stone_p.dds", .type = TextureType::Height },
        { .path = L"textures\\a\\stone_hd_p.dds", .type = TextureType::Height },
    };

    const auto heights = PGNIFUtil::texMatch(L"Textures\\A\\Stone", TextureType::Height, searchMap);
    ASSERT_EQ(heights.size(), 2U);
    std::unordered_set<std::wstring> paths;
    for (const auto& texture : heights) {
        EXPECT_EQ(texture.type, TextureType::Height);
        paths.insert(texture.path.wstring());
    }
    EXPECT_EQ(paths,
              (std::unordered_set<std::wstring> { L"textures\\a\\stone_p.dds", L"textures\\a\\stone_hd_p.dds" }));

    const auto masks = PGNIFUtil::texMatch(L"textures\\a\\stone", TextureType::EnvironmentMask, searchMap);
    ASSERT_EQ(masks.size(), 1U);
    EXPECT_EQ(masks[0].path, L"textures\\a\\stone_m.dds");

    EXPECT_TRUE(PGNIFUtil::texMatch(L"textures\\a\\stone", TextureType::Diffuse, searchMap).empty());
    EXPECT_TRUE(PGNIFUtil::texMatch(L"textures\\a\\other", TextureType::Height, searchMap).empty());
    EXPECT_TRUE(PGNIFUtil::texMatch(L"textures\\a\\stone", TextureType::Height, { }).empty());
}

TEST(PGNIFUtilTest,
     TextureSlotGettersReturnLowerCasePaths)
{
    NIFBuilder builder;
    PGTypes::TextureSet textures;
    textures[0] = L"Textures\\A\\Stone.DDS";
    textures[1] = L"textures\\a\\STONE_n.dds";
    auto* shape = builder.addShape("Stone", textures);

    EXPECT_EQ(PGNIFUtil::textureSlot(&builder.nif(), shape, TextureSlots::Diffuse), "textures\\a\\stone.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&builder.nif(), shape, TextureSlots::Normal), "textures\\a\\stone_n.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&builder.nif(), shape, TextureSlots::Glow), "");

    const auto slots = PGNIFUtil::textureSlots(&builder.nif(), shape);
    EXPECT_EQ(slots[0], L"textures\\a\\stone.dds");
    EXPECT_EQ(slots[1], L"textures\\a\\stone_n.dds");
    for (size_t i = 2; i < numTextureSlots; i++)
        EXPECT_TRUE(slots.at(i).empty()) << i;

    // An effect shader has no texture set, but nifly maps its source texture to the first slot.
    auto* effect = builder.addEffectShape("Effect", "Textures\\FX.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&builder.nif(), effect, TextureSlots::Diffuse), "textures\\fx.dds");
    const auto effectSlots = PGNIFUtil::textureSlots(&builder.nif(), effect);
    EXPECT_EQ(effectSlots[0], L"textures\\fx.dds");
    for (size_t i = 1; i < numTextureSlots; i++)
        EXPECT_TRUE(effectSlots.at(i).empty()) << i;

    // A shape without any shader has only empty slots.
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    EXPECT_EQ(PGNIFUtil::textureSlot(&builder.nif(), shaderless, TextureSlots::Diffuse), "");
    for (const auto& slot : PGNIFUtil::textureSlots(&builder.nif(), shaderless))
        EXPECT_TRUE(slot.empty());
}

TEST(PGNIFUtilTest,
     TextureSlotSettersReportChangesIgnoringCase)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Stone", stoneTextures());
    auto& nif = builder.nif();

    EXPECT_FALSE(PGNIFUtil::setTextureSlot(&nif, shape, TextureSlots::Normal, std::string("Textures\\A\\Stone_N.dds")));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, shape, TextureSlots::Normal), "textures\\a\\stone_n.dds"); // unchanged

    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, shape, TextureSlots::Normal, std::string("textures\\b\\other_n.dds")));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, shape, TextureSlots::Normal), "textures\\b\\other_n.dds");

    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, shape, TextureSlots::Glow, std::wstring(L"textures\\b\\other_g.dds")));
    EXPECT_FALSE(PGNIFUtil::setTextureSlot(&nif, shape, TextureSlots::Glow, std::wstring(L"TEXTURES\\b\\other_g.dds")));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, shape, TextureSlots::Glow), "textures\\b\\other_g.dds");

    // Clearing a slot is a change too.
    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, shape, TextureSlots::Glow, std::string()));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, shape, TextureSlots::Glow), "");

    PGTypes::TextureSet replacement;
    replacement[0] = L"textures\\c\\new.dds";
    replacement[1] = L"textures\\c\\new_n.dds";
    EXPECT_TRUE(PGNIFUtil::setTextureSlots(&nif, shape, replacement));
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, shape), replacement);
    EXPECT_FALSE(PGNIFUtil::setTextureSlots(&nif, shape, replacement));
}

TEST(PGNIFUtilTest,
     TextureSetToStrConvertsEverySlot)
{
    PGTypes::TextureSet set;
    set[0] = L"textures\\a.dds";
    set[8] = L"textures\\ü.dds";
    const auto narrow = PGNIFUtil::textureSetToStr(set);
    EXPECT_EQ(narrow[0], "textures\\a.dds");
    EXPECT_EQ(narrow[1], "");
    EXPECT_EQ(narrow[8], "textures\\\xC3\xBC.dds");
}

TEST(PGNIFUtilTest,
     ShaderSettersReportChanges)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Stone");
    auto* shader = builder.lightingShader(shape);
    ASSERT_NE(shader, nullptr);

    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::setShaderType(shader, nifly::BSLSP_ENVMAP));
    EXPECT_FALSE(PGNIFUtil::setShaderType(shader, nifly::BSLSP_ENVMAP));
    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_ENVMAP);

    float value = 1;
    EXPECT_FALSE(PGNIFUtil::setShaderFloat(value, 1.00001F)); // within tolerance
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(PGNIFUtil::setShaderFloat(value, 2));
    EXPECT_EQ(value, 2);
    EXPECT_TRUE(PGNIFUtil::setShaderFloat(value, 2.5F));
    EXPECT_EQ(value, 2.5F);

    nifly::Vector2 vec(1, 1);
    EXPECT_FALSE(PGNIFUtil::setShaderVec2(vec, nifly::Vector2(1, 1)));
    EXPECT_TRUE(PGNIFUtil::setShaderVec2(vec, nifly::Vector2(2, 1)));
    EXPECT_EQ(vec.u, 2);
    EXPECT_EQ(vec.v, 1);
}

TEST(PGNIFUtilTest,
     ShaderFlagHelpersForBothFlagWords)
{
    NIFBuilder builder;
    auto* shader = builder.lightingShader(builder.addShape("Stone"));
    ASSERT_NE(shader, nullptr);

    // nifly's defaults: flags 1 has ZBUFFER_TEST set and flags 2 has ZBUFFER_WRITE set.
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ZBUFFER_TEST));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_ZBUFFER_WRITE));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));

    EXPECT_TRUE(PGNIFUtil::setShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::setShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ZBUFFER_TEST)); // other flags untouched
    EXPECT_TRUE(PGNIFUtil::clearShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::clearShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_PARALLAX));

    EXPECT_TRUE(PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));
    EXPECT_FALSE(PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));
    EXPECT_TRUE(PGNIFUtil::clearShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));
    EXPECT_FALSE(PGNIFUtil::clearShaderFlag(shader, nifly::SLSF2_VERTEX_COLORS));

    EXPECT_TRUE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING, true));
    EXPECT_FALSE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING, true));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_TRUE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING, false));
    EXPECT_FALSE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING, false));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING));

    EXPECT_TRUE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF2_SOFT_LIGHTING, true));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_SOFT_LIGHTING));
    EXPECT_TRUE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF2_SOFT_LIGHTING, false));
    EXPECT_FALSE(PGNIFUtil::configureShaderFlag(shader, nifly::SLSF2_SOFT_LIGHTING, false));
}

TEST(PGNIFUtilTest,
     LoadNifFromBytesRoundTripsAMesh)
{
    NIFBuilder builder;
    builder.addShape("First", stoneTextures());
    builder.addShape("Second");

    auto loaded = PGNIFUtil::loadNIFFromBytes(builder.bytes());
    ASSERT_TRUE(loaded.IsValid());
    const auto shapes = loaded.GetShapes();
    ASSERT_EQ(shapes.size(), 2U);
    EXPECT_EQ(shapes[0]->name.get(), "First");
    EXPECT_EQ(shapes[1]->name.get(), "Second");
    EXPECT_EQ(PGNIFUtil::textureSlots(&loaded, shapes[0]), stoneTextures());
    EXPECT_TRUE(PGNIFUtil::isShaderPatchableShape(loaded, *shapes[0]));
}

TEST(PGNIFUtilTest,
     LoadNifFromBytesRejectsBadInput)
{
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes({ }), std::runtime_error);
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes({ }, false), std::runtime_error);

    const std::vector<std::byte> garbage(200, std::byte { 'x' });
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes(garbage), std::runtime_error);
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes(garbage, false), std::runtime_error);

    // A truncated mesh.
    NIFBuilder builder;
    builder.addShape("First", stoneTextures());
    auto bytes = builder.bytes();
    bytes.resize(bytes.size() / 2);
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes(bytes), std::runtime_error);
}

TEST(PGNIFUtilTest,
     LoadNifFromBytesChecksTexturePathsAndTextureSetReferences)
{
    // Non-ASCII texture path.
    NIFBuilder nonAscii;
    auto* shape = nonAscii.addShape("Shape", stoneTextures());
    std::string texture = "textures\\caf\xC3\xA9.dds";
    nonAscii.nif().SetTextureSlot(shape, texture, 0);
    const auto nonAsciiBytes = nonAscii.bytes();
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes(nonAsciiBytes), std::runtime_error);
    EXPECT_NO_THROW(PGNIFUtil::loadNIFFromBytes(nonAsciiBytes, false));

    // Dangling texture set reference.
    NIFBuilder dangling;
    auto* shader = dangling.lightingShader(dangling.addShape("Shape", stoneTextures()));
    ASSERT_NE(shader, nullptr);
    shader->TextureSetRef()->index = 500;
    const auto danglingBytes = dangling.bytes();
    EXPECT_THROW(PGNIFUtil::loadNIFFromBytes(danglingBytes), std::runtime_error);
    EXPECT_NO_THROW(PGNIFUtil::loadNIFFromBytes(danglingBytes, false));
}

TEST(PGNIFUtilTest,
     ShapesWith3DIdxCountsShapesAndParticleSystemsInTreeOrder)
{
    NIFBuilder builder;
    builder.addShape("A");
    auto* node = builder.addNode("Node");
    builder.addParticleSystem("Particles", node);
    builder.addShape("B", PGTypes::TextureSet(), nifly::BSLSP_DEFAULT, node);
    builder.addEffectShape("C");
    builder.addNiTriShape("D");

    const auto check = [](const nifly::NifFile& nif) {
        const auto shapes = PGNIFUtil::shapesWith3DIdx(&nif);
        ASSERT_EQ(shapes.size(), 4U);
        EXPECT_EQ(shapes[0].first->name.get(), "A");
        EXPECT_EQ(shapes[0].second, 0);
        EXPECT_EQ(shapes[1].first->name.get(), "B");
        EXPECT_EQ(shapes[1].second, 2); // the particle system took index 1
        EXPECT_EQ(shapes[2].first->name.get(), "C");
        EXPECT_EQ(shapes[2].second, 3);
        EXPECT_EQ(shapes[3].first->name.get(), "D");
        EXPECT_EQ(shapes[3].second, 4);
    };
    check(builder.nif());

    // The order survives saving and loading.
    const auto loaded = PGNIFUtil::loadNIFFromBytes(builder.bytes(), false);
    check(loaded);

    EXPECT_THROW(PGNIFUtil::shapesWith3DIdx(nullptr), std::runtime_error);

    NIFBuilder empty;
    EXPECT_TRUE(PGNIFUtil::shapesWith3DIdx(&empty.nif()).empty());
}

TEST(PGNIFUtilTest,
     IsPatchableShapeNeedsATriShapeWithAShader)
{
    NIFBuilder builder;
    auto* lighting = builder.addShape("Lighting");
    auto* effect = builder.addEffectShape("Effect");
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    auto* legacy = builder.addNiTriShape("Legacy");

    EXPECT_TRUE(PGNIFUtil::isPatchableShape(builder.nif(), *lighting));
    EXPECT_TRUE(PGNIFUtil::isPatchableShape(builder.nif(), *effect));
    EXPECT_FALSE(PGNIFUtil::isPatchableShape(builder.nif(), *shaderless));
    EXPECT_TRUE(PGNIFUtil::isPatchableShape(builder.nif(), *legacy));

    // A shader reference to a block that is not a shader does not count.
    auto* broken = builder.addShaderlessShape("Broken");
    broken->ShaderPropertyRef()->index = builder.nif().GetBlockID(builder.root());
    EXPECT_FALSE(PGNIFUtil::isPatchableShape(builder.nif(), *broken));
}

TEST(PGNIFUtilTest,
     IsShaderPatchableShapeNeedsALightingShaderWithTextures)
{
    NIFBuilder builder;
    auto* lighting = builder.addShape("Lighting", stoneTextures());
    auto* effect = builder.addEffectShape("Effect");
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    auto* legacy = builder.addNiTriShape("Legacy");

    EXPECT_TRUE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *lighting));
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *effect));
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *shaderless));
    EXPECT_TRUE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *legacy));

    // Without a texture set the shape cannot be patched.
    auto* noTextures = builder.addShape("NoTextures");
    builder.lightingShader(noTextures)->TextureSetRef()->Clear();
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *noTextures));
}

TEST(PGNIFUtilTest,
     IsShaderPatchableShapeHonorsThePgIgnoreFlag)
{
    NIFBuilder builder;

    auto* ignoredShape = builder.addShape("IgnoredShape", stoneTextures());
    builder.addBooleanExtraData(ignoredShape, "PG_IGNORE", true);
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *ignoredShape));

    auto* ignoredShader = builder.addShape("IgnoredShader", stoneTextures());
    builder.addBooleanExtraData(builder.lightingShader(ignoredShader), "PG_IGNORE", true);
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *ignoredShader));

    // The flag only counts when it is a boolean set to true with exactly that name.
    auto* notIgnored = builder.addShape("NotIgnored", stoneTextures());
    builder.addBooleanExtraData(notIgnored, "PG_IGNORE", false);
    builder.addBooleanExtraData(notIgnored, "pg_ignore", true);
    builder.addStringExtraData(notIgnored, "PG_IGNORE", "true");
    EXPECT_TRUE(PGNIFUtil::isShaderPatchableShape(builder.nif(), *notIgnored));

    // The flags survive a round trip through the file format.
    auto loaded = PGNIFUtil::loadNIFFromBytes(builder.bytes());
    const auto shapes = loaded.GetShapes();
    ASSERT_EQ(shapes.size(), 3U);
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(loaded, *shapes[0]));
    EXPECT_FALSE(PGNIFUtil::isShaderPatchableShape(loaded, *shapes[1]));
    EXPECT_TRUE(PGNIFUtil::isShaderPatchableShape(loaded, *shapes[2]));
}

TEST(PGNIFUtilTest,
     IsFacegenMeshMatchesTheFacegeomFolderOnly)
{
    EXPECT_TRUE(
        PGNIFUtil::isFacegenMesh(L"meshes\\actors\\character\\facegendata\\facegeom\\skyrim.esm\\00013480.nif"));
    EXPECT_TRUE(PGNIFUtil::isFacegenMesh(L"meshes/actors/character/facegendata/facegeom/skyrim.esm/00013480.nif"));
    EXPECT_TRUE(
        PGNIFUtil::isFacegenMesh(L"Meshes\\Actors\\Character\\FaceGenData\\FaceGeom\\Skyrim.esm\\00013480.NIF"));
    EXPECT_FALSE(
        PGNIFUtil::isFacegenMesh(L"meshes\\actors\\character\\facegendata\\facetint\\skyrim.esm\\00013480.nif"));
    EXPECT_FALSE(PGNIFUtil::isFacegenMesh(L"meshes\\actors\\character\\facegendata\\facegeomx\\a.nif"));
    EXPECT_FALSE(PGNIFUtil::isFacegenMesh(L"meshes\\actors\\character\\character assets\\skeleton.nif"));
    EXPECT_FALSE(PGNIFUtil::isFacegenMesh(L"meshes\\armor\\iron\\cuirass.nif"));
    EXPECT_FALSE(PGNIFUtil::isFacegenMesh(L"textures\\actors\\character\\facegendata\\facegeom\\a.dds"));
    EXPECT_FALSE(PGNIFUtil::isFacegenMesh(L""));
}

TEST(PGNIFUtilTest,
     IsBodySlideShapeDataMeshMatchesTheShapeDataFolderOnly)
{
    EXPECT_TRUE(PGNIFUtil::isBodySlideShapeDataMesh(L"CalienteTools\\BodySlide\\ShapeData\\CBBE\\CBBE Body.nif"));
    EXPECT_TRUE(PGNIFUtil::isBodySlideShapeDataMesh(L"calientetools\\bodyslide\\shapedata\\cbbe\\cbbe body.nif"));
    EXPECT_TRUE(PGNIFUtil::isBodySlideShapeDataMesh(L"calientetools/bodyslide/shapedata/cbbe/cbbe body.nif"));
    EXPECT_FALSE(PGNIFUtil::isBodySlideShapeDataMesh(L"calientetools\\bodyslide\\slidersets\\cbbe.osp"));
    EXPECT_FALSE(PGNIFUtil::isBodySlideShapeDataMesh(L"calientetools\\bodyslide\\shapedatax\\a.nif"));
    EXPECT_FALSE(PGNIFUtil::isBodySlideShapeDataMesh(L"meshes\\armor\\iron\\cuirass.nif"));
    EXPECT_FALSE(PGNIFUtil::isBodySlideShapeDataMesh(L""));
}

}
