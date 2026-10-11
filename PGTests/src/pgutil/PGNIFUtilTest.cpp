#include "pgutil/PGNIFUtil.hpp"

#include "NIFBuilder.hpp"
#include "TestUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/StringUtil.hpp"

#include "Animation.hpp"
#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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

constexpr uint16_t alphaTestFlags = 4844;
constexpr uint16_t alphaBlendFlags = 4845;
constexpr uint8_t defaultThreshold = 128;
constexpr uint8_t lowThreshold = 64;

PGTypes::TextureSet stoneTextures()
{
    PGTypes::TextureSet textures;
    textures[0] = L"textures\\a\\stone.dds";
    textures[1] = L"textures\\a\\stone_n.dds";
    textures[3] = L"textures\\a\\stone_p.dds";
    textures[5] = L"textures\\a\\stone_m.dds";
    return textures;
}

/// @brief Gives the alpha property of a shape an alpha test controller with an interpolator and its data.
void addAlphaController(nifly::NifFile& nif,
                        nifly::NiShape& shape)
{
    auto& header = nif.GetHeader();
    auto* alpha = nif.GetAlphaProperty(&shape);
    ASSERT_NE(alpha, nullptr);

    const auto dataID = header.AddBlock(std::make_unique<nifly::NiFloatData>());
    auto interpolator = std::make_unique<nifly::NiFloatInterpolator>();
    interpolator->dataRef.index = dataID;
    const auto interpolatorID = header.AddBlock(std::move(interpolator));
    auto controller = std::make_unique<nifly::BSNiAlphaPropertyTestRefController>();
    controller->interpolatorRef.index = interpolatorID;
    controller->targetRef.index = nif.GetBlockID(alpha);
    alpha->controllerRef.index = header.AddBlock(std::move(controller));
}

/// @brief Adds a float interpolator with its own data, as the links of a vanilla animation sequence have.
uint32_t addFloatInterpolator(nifly::NifFile& nif)
{
    auto& header = nif.GetHeader();
    auto interpolator = std::make_unique<nifly::NiFloatInterpolator>();
    interpolator->dataRef.index = header.AddBlock(std::make_unique<nifly::NiFloatData>());
    return header.AddBlock(std::move(interpolator));
}

/// @brief Checks that every block but the root is referenced by another block.
void expectNoLooseBlocks(nifly::NifFile& nif)
{
    auto& header = nif.GetHeader();
    for (uint32_t blockID = 0; blockID < header.GetNumBlocks(); blockID++)
        if (header.GetBlock<nifly::NiObject>(blockID) != nif.GetRootNode())
            EXPECT_TRUE(header.IsBlockReferenced(blockID, false)) << header.GetBlockTypeStringById(blockID);
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

    // Nothing to write for shapes without a shader.
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    EXPECT_FALSE(PGNIFUtil::setTextureSlots(&nif, shaderless, replacement));
    EXPECT_FALSE(PGNIFUtil::setTextureSlots(nullptr, shape, replacement));
    EXPECT_FALSE(PGNIFUtil::setTextureSlots(&nif, nullptr, replacement));

    // An effect shader has no texture set; nifly maps its source texture to the first slot, which is written in place.
    auto* effect = builder.addEffectShape("Effect", "textures\\fx.dds");
    const auto numBlocks = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, effect, TextureSlots::Diffuse, std::string("textures\\fx2.dds")));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, effect, TextureSlots::Diffuse), "textures\\fx2.dds");
    EXPECT_FALSE(PGNIFUtil::setTextureSlot(&nif, effect, TextureSlots::Diffuse, std::string("textures\\fx2.dds")));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks);
}

TEST(PGNIFUtilTest,
     TextureSlotSettersCopyASharedTextureSetForTheShapeThatChanges)
{
    NIFBuilder builder;
    auto& nif = builder.nif();
    auto* first = builder.addShape("First", stoneTextures());
    auto* second = builder.addShape("Second");
    auto* third = builder.addShape("Third");
    // Share the texture set block of the first shape, as many vanilla meshes do.
    const auto sharedBlock = builder.lightingShader(first)->textureSetRef.index;
    builder.lightingShader(second)->textureSetRef.index = sharedBlock;
    builder.lightingShader(third)->textureSetRef.index = sharedBlock;
    const auto numBlocks = nif.GetHeader().GetNumBlocks();

    // Writing the textures a shape already has copies nothing.
    EXPECT_FALSE(PGNIFUtil::setTextureSlots(&nif, second, stoneTextures()));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks);

    // A change copies the block for the shape that changes, the others keep the original.
    auto patched = stoneTextures();
    patched[0] = L"textures\\a\\patched.dds";
    EXPECT_TRUE(PGNIFUtil::setTextureSlots(&nif, second, patched));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 1);
    EXPECT_NE(builder.lightingShader(second)->textureSetRef.index, sharedBlock);
    EXPECT_EQ(builder.lightingShader(first)->textureSetRef.index, sharedBlock);
    EXPECT_EQ(builder.lightingShader(third)->textureSetRef.index, sharedBlock);
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, second), patched);
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, first), stoneTextures());
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, third), stoneTextures());

    // A block the shape alone uses changes in place, also through the single slot setter.
    EXPECT_TRUE(
        PGNIFUtil::setTextureSlot(&nif, second, TextureSlots::Normal, std::string("textures\\a\\patched_n.dds")));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 1);
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, second, TextureSlots::Normal), "textures\\a\\patched_n.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, first, TextureSlots::Normal), "textures\\a\\stone_n.dds");

    // The single slot setter copies a shared block too, and the last sharer keeps the original in place.
    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, third, TextureSlots::Glow, std::wstring(L"textures\\a\\stone_g.dds")));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 2);
    EXPECT_NE(builder.lightingShader(third)->textureSetRef.index, sharedBlock);
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, first, TextureSlots::Glow), "");
    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, first, TextureSlots::Glow, std::wstring(L"textures\\a\\stone_g.dds")));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 2);
    EXPECT_EQ(builder.lightingShader(first)->textureSetRef.index, sharedBlock);
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, first, TextureSlots::Glow), "textures\\a\\stone_g.dds");
    // The copies never leave the original without a user (the sets addShape() made for the sharers are loose by
    // construction, so the whole mesh is not checked here).
    EXPECT_TRUE(nif.GetHeader().IsBlockReferenced(sharedBlock, false));
}

TEST(PGNIFUtilTest,
     UnshareShaderCopiesASharedShaderForTheShape)
{
    NIFBuilder builder;
    auto& nif = builder.nif();
    auto* first = builder.addShape("First", stoneTextures());
    auto* second = builder.addShaderlessShape("Second");
    // Share the shader of the first shape, and with it its texture set.
    const auto sharedShader = first->ShaderPropertyRef()->index;
    second->ShaderPropertyRef()->index = sharedShader;
    const auto sharedSet = builder.lightingShader(first)->textureSetRef.index;
    builder.lightingShader(first)->glossiness = 50;
    const auto numBlocks = nif.GetHeader().GetNumBlocks();

    // The shape gets a copy of the shader with the same fields, which still references the shared texture set.
    EXPECT_TRUE(PGNIFUtil::unshareShader(nif, *second));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 1);
    EXPECT_NE(second->ShaderPropertyRef()->index, sharedShader);
    EXPECT_EQ(first->ShaderPropertyRef()->index, sharedShader);
    ASSERT_NE(builder.lightingShader(second), nullptr);
    EXPECT_NE(builder.lightingShader(second), builder.lightingShader(first));
    EXPECT_EQ(builder.lightingShader(second)->glossiness, 50);
    EXPECT_EQ(builder.lightingShader(second)->shaderFlags1, builder.lightingShader(first)->shaderFlags1);
    EXPECT_EQ(builder.lightingShader(second)->textureSetRef.index, sharedSet);
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, second), stoneTextures());

    // Changes to the copy stay with the shape, and its first texture write copies the set as well.
    builder.lightingShader(second)->glossiness = 80;
    EXPECT_EQ(builder.lightingShader(first)->glossiness, 50);
    EXPECT_TRUE(PGNIFUtil::setTextureSlot(&nif, second, TextureSlots::Diffuse, std::string("textures\\a\\other.dds")));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 2);
    EXPECT_NE(builder.lightingShader(second)->textureSetRef.index, sharedSet);
    EXPECT_EQ(builder.lightingShader(first)->textureSetRef.index, sharedSet);
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, first), stoneTextures());
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif, second, TextureSlots::Diffuse), "textures\\a\\other.dds");
    expectNoLooseBlocks(nif);

    // A shader only the shape uses stays as it is, and a shape without a shader has nothing to copy.
    EXPECT_FALSE(PGNIFUtil::unshareShader(nif, *second));
    EXPECT_FALSE(PGNIFUtil::unshareShader(nif, *first));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 2);
    auto* shaderless = builder.addShaderlessShape("Shaderless");
    EXPECT_FALSE(PGNIFUtil::unshareShader(nif, *shaderless));

    // A shader with a controller stays shared, since the controller animates that one block.
    auto* animated = builder.addShape("Animated", stoneTextures());
    auto* animatedSharer = builder.addShaderlessShape("AnimatedSharer");
    animatedSharer->ShaderPropertyRef()->index = animated->ShaderPropertyRef()->index;
    auto controller = std::make_unique<nifly::BSLightingShaderPropertyFloatController>();
    controller->targetRef.index = animated->ShaderPropertyRef()->index;
    builder.lightingShader(animated)->controllerRef.index = nif.GetHeader().AddBlock(std::move(controller));
    const auto blocksWithController = nif.GetHeader().GetNumBlocks();
    EXPECT_FALSE(PGNIFUtil::unshareShader(nif, *animatedSharer));
    EXPECT_EQ(animatedSharer->ShaderPropertyRef()->index, animated->ShaderPropertyRef()->index);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithController);

    // An effect shader is never copied: the shape patchers do not write one.
    auto* effect = builder.addEffectShape("Effect", "textures\\fx.dds");
    auto* effectSharer = builder.addShaderlessShape("EffectSharer");
    effectSharer->ShaderPropertyRef()->index = effect->ShaderPropertyRef()->index;
    const auto blocksWithEffect = nif.GetHeader().GetNumBlocks();
    EXPECT_FALSE(PGNIFUtil::unshareShader(nif, *effectSharer));
    EXPECT_EQ(effectSharer->ShaderPropertyRef()->index, effect->ShaderPropertyRef()->index);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithEffect);
}

TEST(PGNIFUtilTest,
     SetAlphaPropertyCreatesOnlyOnRequestAndCopiesSharedBlocks)
{
    NIFBuilder builder;
    auto& nif = builder.nif();
    auto* shape = builder.addShape("Shape", stoneTextures());
    const auto numBlocks = nif.GetHeader().GetNumBlocks();

    // Without the create flag a shape without an alpha property is left alone.
    EXPECT_FALSE(PGNIFUtil::setAlphaProperty(nif, *shape, alphaBlendFlags, lowThreshold, false));
    EXPECT_EQ(nif.GetAlphaProperty(shape), nullptr);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks);

    // The create flag adds one, with the default for every value that is not given.
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(nif, *shape, std::nullopt, lowThreshold, true));
    auto* alpha = nif.GetAlphaProperty(shape);
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->flags, alphaTestFlags);
    EXPECT_EQ(alpha->threshold, lowThreshold);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 1);

    // The same values change nothing, other values change the block in place, with or without the create flag.
    EXPECT_FALSE(PGNIFUtil::setAlphaProperty(nif, *shape, alphaTestFlags, lowThreshold, true));
    EXPECT_FALSE(PGNIFUtil::setAlphaProperty(nif, *shape, std::nullopt, std::nullopt, false));
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(nif, *shape, alphaBlendFlags, std::nullopt, false));
    EXPECT_EQ(alpha->flags, alphaBlendFlags);
    EXPECT_EQ(alpha->threshold, lowThreshold);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 1);

    // A shared alpha property is copied for the shape that changes, the other shape keeps the original.
    auto* sharer = builder.addShape("Sharer", stoneTextures());
    sharer->AlphaPropertyRef()->index = shape->AlphaPropertyRef()->index;
    const auto blocksWithSharer = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(nif, *sharer, std::nullopt, defaultThreshold, false));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithSharer + 1);
    EXPECT_NE(sharer->AlphaPropertyRef()->index, shape->AlphaPropertyRef()->index);
    EXPECT_EQ(nif.GetAlphaProperty(shape), alpha);
    EXPECT_EQ(alpha->threshold, lowThreshold);
    ASSERT_NE(nif.GetAlphaProperty(sharer), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(sharer)->flags, alphaBlendFlags);
    EXPECT_EQ(nif.GetAlphaProperty(sharer)->threshold, defaultThreshold);

    // An alpha property with a controller stays shared: the controller animates that one block.
    auto* animated = builder.addShape("Animated", stoneTextures());
    auto* animatedSharer = builder.addShape("AnimatedSharer", stoneTextures());
    const auto* const animatedAlpha = builder.addAlphaProperty(animated);
    animatedSharer->AlphaPropertyRef()->index = animated->AlphaPropertyRef()->index;
    addAlphaController(nif, *animated);
    const auto blocksWithController = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(nif, *animatedSharer, alphaBlendFlags, std::nullopt, false));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithController);
    EXPECT_EQ(animatedSharer->AlphaPropertyRef()->index, animated->AlphaPropertyRef()->index);
    EXPECT_EQ(animatedAlpha->flags, alphaBlendFlags);
    expectNoLooseBlocks(nif);
}

TEST(PGNIFUtilTest,
     DeleteAlphaPropertyOnlyDeletesWhatTheShapeAloneUses)
{
    NIFBuilder builder;
    auto& nif = builder.nif();
    auto* shape = builder.addShape("Shape", stoneTextures());
    EXPECT_FALSE(PGNIFUtil::deleteAlphaProperty(nif, *shape)); // nothing to delete

    // A shared block stays for the other shape.
    auto* sharer = builder.addShape("Sharer", stoneTextures());
    auto* alpha = builder.addAlphaProperty(shape, alphaBlendFlags, lowThreshold);
    sharer->AlphaPropertyRef()->index = shape->AlphaPropertyRef()->index;
    const auto numBlocks = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *shape));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks);
    EXPECT_EQ(nif.GetAlphaProperty(shape), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(sharer), alpha);
    EXPECT_FALSE(PGNIFUtil::deleteAlphaProperty(nif, *shape));

    // A block only the shape uses goes, together with its controller, the interpolator and the data of that one.
    addAlphaController(nif, *sharer);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks + 3);
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *sharer));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks - 1);
    EXPECT_EQ(nif.GetAlphaProperty(sharer), nullptr);
    expectNoLooseBlocks(nif);

    // The shapes and their shaders are untouched by the deletion.
    EXPECT_EQ(nif.GetShapes().size(), 2U);
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, shape), stoneTextures());
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif, sharer), stoneTextures());

    // A controller an animation sequence drives goes with the property, and the link of the sequence goes with the
    // controller, together with the interpolator only that link used. The link to another controller stays.
    auto* animated = builder.addShape("Animated", stoneTextures());
    const auto* const animatedAlpha = builder.addAlphaProperty(animated);
    addAlphaController(nif, *animated);
    auto shaderController = std::make_unique<nifly::BSLightingShaderPropertyFloatController>();
    auto* const shaderControllerPtr = shaderController.get();
    shaderController->interpolatorRef.index = addFloatInterpolator(nif);
    shaderController->targetRef.index = animated->ShaderPropertyRef()->index;
    const auto shaderControllerID = nif.GetHeader().AddBlock(std::move(shaderController));
    builder.lightingShader(animated)->controllerRef.index = shaderControllerID;
    auto sequence = std::make_unique<nifly::NiControllerSequence>();
    auto* const sequencePtr = sequence.get();
    nifly::ControllerLink alphaLink;
    alphaLink.controllerRef.index = animatedAlpha->controllerRef.index;
    alphaLink.interpolatorRef.index = addFloatInterpolator(nif);
    sequence->controlledBlocks.push_back(alphaLink);
    nifly::ControllerLink shaderLink;
    shaderLink.controllerRef.index = shaderControllerID;
    shaderLink.interpolatorRef.index = addFloatInterpolator(nif);
    sequence->controlledBlocks.push_back(shaderLink);
    auto manager = std::make_unique<nifly::NiControllerManager>();
    auto* const managerPtr = manager.get();
    manager->controllerSequenceRefs.AddBlockRef(nif.GetHeader().AddBlock(std::move(sequence)));
    manager->targetRef.index = nif.GetBlockID(nif.GetRootNode());
    nif.GetRootNode()->controllerRef.index = nif.GetHeader().AddBlock(std::move(manager));
    const auto blocksWithSequence = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *animated));
    EXPECT_EQ(nif.GetAlphaProperty(animated), nullptr);
    // The property, its controller, interpolator and data, and the interpolator and data of the erased link.
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithSequence - 6);
    ASSERT_EQ(sequencePtr->controlledBlocks.size(), 1U);
    EXPECT_EQ(sequencePtr->controlledBlocks[0].controllerRef.index, nif.GetBlockID(shaderControllerPtr));
    expectNoLooseBlocks(nif);

    // Two links of a sequence share one interpolator, which the controller of the first shape uses as well. Deleting
    // that shape's property keeps the interpolator for the link of the other controller, which stays.
    auto* twinA = builder.addShape("TwinA", stoneTextures());
    auto* twinB = builder.addShape("TwinB", stoneTextures());
    const auto* const twinAlphaA = builder.addAlphaProperty(twinA);
    const auto* const twinAlphaB = builder.addAlphaProperty(twinB);
    addAlphaController(nif, *twinA);
    addAlphaController(nif, *twinB);
    const auto* const twinControllerA
        = nif.GetHeader().GetBlock<nifly::NiSingleInterpController>(twinAlphaA->controllerRef.index);
    ASSERT_NE(twinControllerA, nullptr);
    auto* const sharedInterpolator = nif.GetHeader().GetBlock<nifly::NiObject>(twinControllerA->interpolatorRef.index);
    ASSERT_NE(sharedInterpolator, nullptr);
    auto twinSequence = std::make_unique<nifly::NiControllerSequence>();
    auto* const twinSequencePtr = twinSequence.get();
    nifly::ControllerLink linkA;
    linkA.controllerRef.index = twinAlphaA->controllerRef.index;
    linkA.interpolatorRef.index = twinControllerA->interpolatorRef.index;
    twinSequence->controlledBlocks.push_back(linkA);
    nifly::ControllerLink linkB;
    linkB.controllerRef.index = twinAlphaB->controllerRef.index;
    linkB.interpolatorRef.index = twinControllerA->interpolatorRef.index;
    twinSequence->controlledBlocks.push_back(linkB);
    managerPtr->controllerSequenceRefs.AddBlockRef(nif.GetHeader().AddBlock(std::move(twinSequence)));
    const auto blocksWithTwins = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *twinA));
    EXPECT_EQ(nif.GetAlphaProperty(twinA), nullptr);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithTwins - 2); // the property and its controller only
    ASSERT_NE(nif.GetAlphaProperty(twinB), nullptr);
    ASSERT_EQ(twinSequencePtr->controlledBlocks.size(), 1U);
    EXPECT_EQ(twinSequencePtr->controlledBlocks[0].controllerRef.index, twinAlphaB->controllerRef.index);
    EXPECT_EQ(twinSequencePtr->controlledBlocks[0].interpolatorRef.index, nif.GetBlockID(sharedInterpolator));
    expectNoLooseBlocks(nif);

    // A block of the tree that another block uses stays with what it reaches, the rest of the tree goes.
    auto* partial = builder.addShape("Partial", stoneTextures());
    const auto* const partialAlpha = builder.addAlphaProperty(partial);
    addAlphaController(nif, *partial);
    const auto* const partialController
        = nif.GetHeader().GetBlock<nifly::NiSingleInterpController>(partialAlpha->controllerRef.index);
    ASSERT_NE(partialController, nullptr);
    const auto sharedInterpolatorID = partialController->interpolatorRef.index;
    auto partialShaderController = std::make_unique<nifly::BSLightingShaderPropertyFloatController>();
    partialShaderController->interpolatorRef.index = sharedInterpolatorID;
    partialShaderController->targetRef.index = partial->ShaderPropertyRef()->index;
    builder.lightingShader(partial)->controllerRef.index = nif.GetHeader().AddBlock(std::move(partialShaderController));
    const auto blocksWithPartial = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *partial));
    EXPECT_EQ(nif.GetAlphaProperty(partial), nullptr);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithPartial - 2); // property and controller only
    const auto* const keptShaderController = nif.GetHeader().GetBlock<nifly::NiSingleInterpController>(
        builder.lightingShader(partial)->controllerRef.index);
    ASSERT_NE(keptShaderController, nullptr);
    const auto* const keptInterpolator
        = nif.GetHeader().GetBlock<nifly::NiFloatInterpolator>(keptShaderController->interpolatorRef.index);
    ASSERT_NE(keptInterpolator, nullptr);
    EXPECT_NE(nif.GetHeader().GetBlock<nifly::NiFloatData>(keptInterpolator->dataRef.index), nullptr);

    // A broken file whose alpha property references a scene graph block: nodes and shapes are never deleted.
    auto* broken = builder.addShape("Broken", stoneTextures());
    auto* brokenAlpha = builder.addAlphaProperty(broken);
    brokenAlpha->controllerRef.index = nif.GetBlockID(nif.GetRootNode());
    const auto blocksWithBroken = nif.GetHeader().GetNumBlocks();
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif, *broken));
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), blocksWithBroken - 1);
    EXPECT_EQ(nif.GetAlphaProperty(broken), nullptr);
    ASSERT_NE(nif.GetRootNode(), nullptr);
    EXPECT_EQ(nif.GetShapes().size(), 7U);
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
