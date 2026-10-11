#include "pgutil/PGTypes.hpp"

#include "pgutil/PGEnums.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

namespace {

TEST(PGTypesTest,
     TextureSlotsFromStrFillsLeadingSlots)
{
    static_assert(numTextureSlots == 9);
    static_assert(std::tuple_size_v<PGTypes::TextureSet> == numTextureSlots);

    const auto two = PGTypes::textureSlotsFromStr("textures\\a.dds,textures\\a_n.dds");
    EXPECT_EQ(two[0], L"textures\\a.dds");
    EXPECT_EQ(two[1], L"textures\\a_n.dds");
    for (size_t i = 2; i < numTextureSlots; i++)
        EXPECT_TRUE(two.at(i).empty()) << i;

    const auto none = PGTypes::textureSlotsFromStr("");
    for (const auto& slot : none)
        EXPECT_TRUE(slot.empty());

    const auto gaps = PGTypes::textureSlotsFromStr(",,x.dds");
    EXPECT_TRUE(gaps[0].empty());
    EXPECT_TRUE(gaps[1].empty());
    EXPECT_EQ(gaps[2], L"x.dds");

    const auto utf8 = PGTypes::textureSlotsFromStr("\xC3\xBC.dds");
    EXPECT_EQ(utf8[0], L"ü.dds");
}

TEST(PGTypesTest,
     TextureSlotsFromStrDropsEntriesBeyondTheSlotCount)
{
    const auto slots = PGTypes::textureSlotsFromStr("0,1,2,3,4,5,6,7,8,9,10");
    for (size_t i = 0; i < numTextureSlots; i++)
        EXPECT_EQ(slots.at(i), std::to_wstring(i));
}

TEST(PGTypesTest,
     StrFromTextureSlotsJoinsEverySlot)
{
    PGTypes::TextureSet slots;
    EXPECT_EQ(PGTypes::strFromTextureSlots(slots), ",,,,,,,,");

    slots[0] = L"a.dds";
    slots[8] = L"z.dds";
    EXPECT_EQ(PGTypes::strFromTextureSlots(slots), "a.dds,,,,,,,,z.dds");

    slots[3] = L"ü.dds";
    EXPECT_EQ(PGTypes::textureSlotsFromStr(PGTypes::strFromTextureSlots(slots)), slots);
}

TEST(PGTypesTest,
     TextureSetHashDependsOnContentAndPosition)
{
    PGTypes::TextureSet a;
    a[0] = L"diffuse.dds";
    a[1] = L"normal.dds";
    const PGTypes::TextureSet same = a;
    PGTypes::TextureSet swapped;
    swapped[0] = L"normal.dds";
    swapped[1] = L"diffuse.dds";
    PGTypes::TextureSet other = a;
    other[2] = L"glow.dds";

    const PGTypes::TextureSetHash hash;
    EXPECT_EQ(hash(a), hash(same));
    EXPECT_NE(hash(a), hash(swapped));
    EXPECT_NE(hash(a), hash(other));
    EXPECT_NE(hash(a), hash(PGTypes::TextureSet { }));

    const std::unordered_set<PGTypes::TextureSet, PGTypes::TextureSetHash> set { a, same, swapped };
    EXPECT_EQ(set.size(), 2U);
}

TEST(PGTypesTest,
     PGTextureEqualityAndHash)
{
    const PGTypes::PGTexture diffuse { .path = L"textures\\a.dds", .type = PGEnums::TextureType::Diffuse };
    const PGTypes::PGTexture sameDiffuse { .path = L"textures\\a.dds", .type = PGEnums::TextureType::Diffuse };
    const PGTypes::PGTexture normal { .path = L"textures\\a.dds", .type = PGEnums::TextureType::Normal };
    const PGTypes::PGTexture otherPath { .path = L"textures\\b.dds", .type = PGEnums::TextureType::Diffuse };

    EXPECT_EQ(diffuse, sameDiffuse);
    EXPECT_FALSE(diffuse == normal);
    EXPECT_FALSE(diffuse == otherPath);

    const PGTypes::PGTextureHasher hasher;
    EXPECT_EQ(hasher(diffuse), hasher(sameDiffuse));
    EXPECT_NE(hasher(diffuse), hasher(normal));
    EXPECT_NE(hasher(diffuse), hasher(otherPath));

    const std::unordered_set<PGTypes::PGTexture, PGTypes::PGTextureHasher> set {
        diffuse,
        sameDiffuse,
        normal,
        otherPath,
    };
    EXPECT_EQ(set.size(), 3U);
}

TEST(PGTypesTest,
     CMClassificationEquality)
{
    const PGTypes::CMClassification none;
    PGTypes::CMClassification cm;
    cm.isCM = true;
    cm.hasEnvMask = true;
    PGTypes::CMClassification sameCM = cm;

    EXPECT_EQ(none, PGTypes::CMClassification { });
    EXPECT_EQ(cm, sameCM);
    EXPECT_NE(cm, none);
    sameCM.hasMetalness = true;
    EXPECT_NE(cm, sameCM);
}

TEST(PGTypesTest,
     TextureVoteHoldsItsFields)
{
    const PGTypes::TextureVote vote {
        .texture = L"textures\\a_m.dds",
        .slot = PGEnums::TextureSlots::EnvMask,
        .type = PGEnums::TextureType::EnvironmentMask,
    };
    EXPECT_EQ(vote.texture, L"textures\\a_m.dds");
    EXPECT_EQ(vote.slot, PGEnums::TextureSlots::EnvMask);
    EXPECT_EQ(vote.type, PGEnums::TextureType::EnvironmentMask);
}

}
