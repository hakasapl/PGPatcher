#include "pgutil/PGEnums.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

namespace {

TEST(PGEnumsTest,
     ShaderNamesRoundTrip)
{
    const std::vector<std::pair<PGEnums::ShapeShader, std::string>> expected {
        { PGEnums::ShapeShader::None, "Default" },
        { PGEnums::ShapeShader::Unknown, "Unknown" },
        { PGEnums::ShapeShader::TruePBR, "PBR" },
        { PGEnums::ShapeShader::ComplexMaterial, "Complex Material" },
        { PGEnums::ShapeShader::VanillaParallax, "Parallax" },
    };
    for (const auto& [shader, name] : expected) {
        EXPECT_EQ(PGEnums::strFromShader(shader), name);
        EXPECT_EQ(PGEnums::shaderFromStr(name), shader);
    }

    EXPECT_EQ(PGEnums::shaderFromStr("pbr"), PGEnums::ShapeShader::Unknown); // case-sensitive
    EXPECT_EQ(PGEnums::shaderFromStr(""), PGEnums::ShapeShader::Unknown);
    EXPECT_EQ(PGEnums::strFromShader(static_cast<PGEnums::ShapeShader>(200)), "Unknown");
}

TEST(PGEnumsTest,
     ShadersAreOrderedFromWorstToBest)
{
    EXPECT_LT(PGEnums::ShapeShader::Unknown, PGEnums::ShapeShader::None);
    EXPECT_LT(PGEnums::ShapeShader::None, PGEnums::ShapeShader::VanillaParallax);
    EXPECT_LT(PGEnums::ShapeShader::VanillaParallax, PGEnums::ShapeShader::ComplexMaterial);
    EXPECT_LT(PGEnums::ShapeShader::ComplexMaterial, PGEnums::ShapeShader::TruePBR);
}

TEST(PGEnumsTest,
     TextureTypeNamesRoundTrip)
{
    for (const auto& entry : PGEnums::textureTypeTable) {
        EXPECT_EQ(PGEnums::strFromTexType(entry.value), entry.name);
        EXPECT_EQ(PGEnums::texTypeFromStr(std::string(entry.name)), entry.value);
    }

    EXPECT_EQ(PGEnums::texTypeFromStr("Diffuse"), PGEnums::TextureType::Unknown); // case-sensitive
    EXPECT_EQ(PGEnums::texTypeFromStr("not a type"), PGEnums::TextureType::Unknown);
    EXPECT_EQ(PGEnums::strFromTexType(static_cast<PGEnums::TextureType>(200)), "unknown");
}

TEST(PGEnumsTest,
     TextureTypeTableCoversEveryEnumerator)
{
    static_assert(static_cast<size_t>(PGEnums::TextureType::Unknown) + 1 == PGEnums::textureTypeTable.size());

    const auto names = PGEnums::texTypesStr();
    ASSERT_EQ(names.size(), PGEnums::textureTypeTable.size());
    for (size_t i = 0; i < names.size(); i++)
        EXPECT_EQ(names[i], PGEnums::textureTypeTable.at(i).name);

    // Every enumerator maps to a distinct name.
    for (size_t i = 0; i <= static_cast<size_t>(PGEnums::TextureType::Unknown); i++) {
        const auto type = static_cast<PGEnums::TextureType>(i);
        const auto name = PGEnums::strFromTexType(type);
        EXPECT_EQ(PGEnums::texTypeFromStr(name), type) << name;
    }
}

TEST(PGEnumsTest,
     TextureSlotsMatchTheNifSlotIndices)
{
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Diffuse), 0);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Normal), 1);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Glow), 2);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Parallax), 3);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Cubemap), 4);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::EnvMask), 5);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::MultiLayer), 6);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Backlight), 7);
    EXPECT_EQ(static_cast<int>(PGEnums::TextureSlots::Unused), 8);
}

}
