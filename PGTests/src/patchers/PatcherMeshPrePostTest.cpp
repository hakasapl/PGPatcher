#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "patchers/PatcherMeshPostHairFlowMap.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixMeshLighting.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

namespace {

using PGEnums::TextureSlots;
using PGEnums::TextureType;
using TestUtil::NIFBuilder;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    return set;
}

/**
 * @brief A data folder with a few textures, mapped by the global PGDirectory, and a mesh with one default shader shape.
 */
class PatcherMeshPrePostTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    NIFBuilder m_builder;
    nifly::BSTriShape* m_shape = nullptr;
    nifly::BSLightingShaderProperty* m_shader = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }

    void SetUp() override
    {
        for (const auto* texture : {
                 L"textures\\a\\stone.dds",
                 L"textures\\a\\stone_n.dds",
                 L"textures\\a\\stone_p.dds",
                 L"textures\\a\\stone_m.dds",
                 L"textures\\cubemaps\\c_e.dds",
                 L"textures\\a\\hair_n.dds",
                 L"textures\\a\\hair_flow.dds",
             }) {
            TestUtil::writeFile(dataDir() / texture, "DDS");
        }

        m_shape = m_builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        m_shader = m_builder.lightingShader(m_shape);
        m_builder.save(dataDir() / meshPath);

        m_pgd = std::make_unique<PGDirectory>(dataDir(), m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        // The flow map as the texture mapping would list it.
        m_pgd->textureMap(TextureSlots::Backlight)[L"textures\\a\\hair"].insert(
            { .path = L"textures\\a\\hair_flow.dds", .type = TextureType::HairFlowMap });
        m_pgd->setTextureType(L"textures\\a\\hair_flow.dds", TextureType::HairFlowMap);
        PGGlobals::setPGD(m_pgd.get());
    }

    void TearDown() override
    {
        PatcherTextureHookFixSSS::reset();
        PGGlobals::setPGD(nullptr);
    }

    [[nodiscard]] PGTypes::TextureSet currentSlots() { return PGNIFUtil::textureSlots(&m_builder.nif(), m_shape); }
};

TEST_F(PatcherMeshPrePostTest,
       FixMeshLightingClampsSoftLightingOfDefaultShaders)
{
    PatcherMeshPreFixMeshLighting patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "FixMeshLighting");
    auto patched = currentSlots();

    m_shader->softlighting = 0.3F;
    EXPECT_FALSE(patcher.applyPatch(patched, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.3F);
    m_shader->softlighting = 0.6F;
    EXPECT_FALSE(patcher.applyPatch(patched, *m_shape));

    m_shader->softlighting = 0.9F;
    EXPECT_TRUE(patcher.applyPatch(patched, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.6F);
    EXPECT_EQ(patched, currentSlots()); // the slots are not touched

    // Only the default shader type is fixed.
    m_shader->softlighting = 0.9F;
    m_shader->SetShaderType(nifly::BSLSP_SKINTINT);
    EXPECT_FALSE(patcher.applyPatch(patched, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.9F);

    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.applyPatch(patched, *effect));

    const auto fromFactory = PatcherMeshPreFixMeshLighting::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "FixMeshLighting");
}

TEST_F(PatcherMeshPrePostTest,
       FixTextureSlotCountGrowsShortTextureSets)
{
    PatcherMeshPreFixTextureSlotCount patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "FixTextureSlotCount");
    auto patched = currentSlots();

    auto* textureSet = m_builder.textureSet(m_shape);
    ASSERT_EQ(textureSet->textures.size(), 9U);
    EXPECT_FALSE(patcher.applyPatch(patched, *m_shape));

    textureSet->textures.resize(6);
    EXPECT_TRUE(patcher.applyPatch(patched, *m_shape));
    EXPECT_EQ(textureSet->textures.size(), 9U);
    EXPECT_EQ(currentSlots(), slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));

    // Larger sets are left as they are.
    textureSet->textures.resize(10);
    EXPECT_FALSE(patcher.applyPatch(patched, *m_shape));
    EXPECT_EQ(textureSet->textures.size(), 10U);

    // Shapes without a shader or without a texture set are skipped.
    auto* bare = m_builder.addShaderlessShape("Bare");
    EXPECT_FALSE(patcher.applyPatch(patched, *bare));
    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.applyPatch(patched, *effect));

    const auto fromFactory = PatcherMeshPreFixTextureSlotCount::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "FixTextureSlotCount");
}

TEST_F(PatcherMeshPrePostTest,
       HairFlowMapIsAssignedToHairWithAFlowMap)
{
    PatcherMeshPostHairFlowMap patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "HairFlowMap");

    auto hair = slots(L"textures\\a\\hair.dds", L"textures\\a\\hair_n.dds");
    m_shader->SetShaderType(nifly::BSLSP_HAIRTINT);
    EXPECT_TRUE(patcher.applyPatch(hair, *m_shape));
    EXPECT_EQ(hair[7], L"textures\\a\\hair_flow.dds");
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING));

    // Hair that has back lighting already is left alone, as is anything that is not hair.
    auto again = slots(L"textures\\a\\hair.dds", L"textures\\a\\hair_n.dds");
    EXPECT_FALSE(patcher.applyPatch(again, *m_shape));
    EXPECT_TRUE(again[7].empty());
    PGNIFUtil::clearShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING);
    m_shader->SetShaderType(nifly::BSLSP_DEFAULT);
    EXPECT_FALSE(patcher.applyPatch(again, *m_shape));
    m_shader->SetShaderType(nifly::BSLSP_HAIRTINT);

    // Without a normal map that exists, or without a flow map for it, nothing happens.
    auto noNormal = slots(L"textures\\a\\hair.dds");
    EXPECT_FALSE(patcher.applyPatch(noNormal, *m_shape));
    auto missingNormal = slots(L"textures\\a\\hair.dds", L"textures\\a\\missing_n.dds");
    EXPECT_FALSE(patcher.applyPatch(missingNormal, *m_shape));
    auto otherNormal = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    EXPECT_FALSE(patcher.applyPatch(otherNormal, *m_shape));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING));

    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.applyPatch(hair, *effect));

    const auto fromFactory = PatcherMeshPostHairFlowMap::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "HairFlowMap");
}

TEST_F(PatcherMeshPrePostTest,
       FixSSSRegistersATextureHookWhenTheGlowMapIsTheDiffuse)
{
    PatcherMeshPostFixSSS patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "FixSSS");
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING);

    auto skin = slots(L"textures\\a\\stone.dds");
    skin[2] = L"textures\\a\\STONE.dds"; // the comparison ignores case
    EXPECT_TRUE(patcher.applyPatch(skin, *m_shape));
    EXPECT_EQ(skin[2], L"textures\\a\\stone_s.dds");
    EXPECT_EQ(skin[0], L"textures\\a\\stone.dds");
    EXPECT_TRUE(PatcherTextureHookFixSSS::isInProcessList(L"textures\\a\\stone.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\a\\stone_s.dds"));

    // A glow map of its own, no soft lighting, another shader type, no texture or a texture that is not a DDS file
    // are all left alone.
    auto other = slots(L"textures\\a\\stone.dds");
    other[2] = L"textures\\a\\stone_g.dds";
    EXPECT_FALSE(patcher.applyPatch(other, *m_shape));
    EXPECT_EQ(other[2], L"textures\\a\\stone_g.dds");
    auto same = slots(L"textures\\a\\stone.dds");
    same[2] = L"textures\\a\\stone.dds";
    PGNIFUtil::clearShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING);
    EXPECT_FALSE(patcher.applyPatch(same, *m_shape));
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING);
    m_shader->SetShaderType(nifly::BSLSP_SKINTINT);
    EXPECT_FALSE(patcher.applyPatch(same, *m_shape));
    m_shader->SetShaderType(nifly::BSLSP_DEFAULT);
    PGTypes::TextureSet empty;
    EXPECT_FALSE(patcher.applyPatch(empty, *m_shape));
    auto notDDS = slots(L"textures\\a\\stone.png");
    notDDS[2] = L"textures\\a\\stone.png";
    EXPECT_FALSE(patcher.applyPatch(notDDS, *m_shape));
    EXPECT_EQ(same[2], L"textures\\a\\stone.dds");

    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.applyPatch(same, *effect));

    const auto fromFactory = PatcherMeshPostFixSSS::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "FixSSS");
}

TEST_F(PatcherMeshPrePostTest,
       RestoreDefaultShadersRevertsParallaxWithoutAHeightMap)
{
    PatcherMeshPostRestoreDefaultShaders patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "RestoreDefaultShaders");

    m_shader->SetShaderType(nifly::BSLSP_PARALLAX);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_PARALLAX);
    auto parallax = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    parallax[3] = L"textures\\a\\STONE_P.dds";
    EXPECT_FALSE(patcher.applyPatch(parallax, *m_shape)); // the height map exists
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_PARALLAX);

    parallax[3] = L"textures\\a\\missing_p.dds";
    EXPECT_TRUE(patcher.applyPatch(parallax, *m_shape));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_PARALLAX));
    EXPECT_TRUE(parallax[3].empty());
    EXPECT_EQ(parallax[0], L"textures\\a\\stone.dds");

    // A default shader is left alone, and so are shapes without a lighting shader.
    EXPECT_FALSE(patcher.applyPatch(parallax, *m_shape));
    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.applyPatch(parallax, *effect));
}

TEST_F(PatcherMeshPrePostTest,
       RestoreDefaultShadersRevertsEnvironmentMapsWithMissingTextures)
{
    PatcherMeshPostRestoreDefaultShaders patcher(meshPath, &m_builder.nif());

    const auto envMap = [this](const std::wstring& cubemap, const std::wstring& envMask) {
        m_shader->SetShaderType(nifly::BSLSP_ENVMAP);
        PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
        auto set = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
        set[4] = cubemap;
        set[5] = envMask;
        return set;
    };

    // Existing textures, empty slots and the dynamic cubemap are all valid.
    auto valid = envMap(L"textures\\cubemaps\\c_e.dds", L"textures\\a\\stone_m.dds");
    EXPECT_FALSE(patcher.applyPatch(valid, *m_shape));
    auto empty = envMap(L"", L"");
    EXPECT_FALSE(patcher.applyPatch(empty, *m_shape));
    auto dynamic = envMap(PatcherMeshShaderComplexMaterial::s_dynCubemapPath.wstring(), L"textures\\a\\stone_m.dds");
    EXPECT_FALSE(patcher.applyPatch(dynamic, *m_shape));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_ENVMAP);

    auto missingMask = envMap(L"textures\\cubemaps\\c_e.dds", L"textures\\a\\missing_m.dds");
    EXPECT_TRUE(patcher.applyPatch(missingMask, *m_shape));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_TRUE(missingMask[4].empty());
    EXPECT_TRUE(missingMask[5].empty());

    auto missingCubemap = envMap(L"textures\\cubemaps\\missing_e.dds", L"textures\\a\\stone_m.dds");
    EXPECT_TRUE(patcher.applyPatch(missingCubemap, *m_shape));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(missingCubemap[4].empty());
    EXPECT_TRUE(missingCubemap[5].empty());

    const auto fromFactory = PatcherMeshPostRestoreDefaultShaders::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "RestoreDefaultShaders");
}

}
