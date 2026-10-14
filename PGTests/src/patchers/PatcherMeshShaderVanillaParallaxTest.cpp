#include "patchers/PatcherMeshShaderVanillaParallax.hpp"

#include "DDSBuilder.hpp"
#include "NIFBuilder.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using PGEnums::TextureSlots;
using PGEnums::TextureType;
using PatcherMatch = PatcherMeshShader::PatcherMatch;
using ModelRecordType = PGPlugin::ModelRecordType;
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
 * @brief A data folder with textures of two aspect ratios and their height maps, mapped by the global PGDirectory, with
 * a global PGD3D for the aspect ratio checks (which read texture headers only).
 */
class PatcherMeshShaderVanillaParallaxTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d;
    NIFBuilder m_builder;
    nifly::BSTriShape* m_shape = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }

    void SetUp() override
    {
        for (const auto* texture : {
                 L"textures\\a\\stone.dds",
                 L"textures\\a\\stone_n.dds",
                 L"textures\\a\\stone_p.dds",
                 L"textures\\a\\alt_p.dds",
                 L"textures\\a\\wide_p.dds",
             }) {
            TestUtil::DDSBuilder::writeDDS(dataDir() / texture, 64, 64);
        }
        for (const auto* texture : { L"textures\\a\\wide.dds", L"textures\\a\\wide_n.dds" })
            TestUtil::DDSBuilder::writeDDS(dataDir() / texture, 128, 64);

        m_shape = m_builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        m_builder.save(dataDir() / meshPath);

        m_pgd = std::make_unique<PGDirectory>(dataDir(), m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        m_pgd->mapFiles({ }, { }, { }, { }, false);
        m_pgd->waitForMeshMapping();
        PGGlobals::setPGD(m_pgd.get());
        PGGlobals::setPGD3D(&m_d3d);
    }

    void TearDown() override
    {
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
    }
};

TEST_F(PatcherMeshShaderVanillaParallaxTest,
       CanApplyRejectsWhatParallaxCannotRender)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderVanillaParallax patcher(meshPath, &nif);
    EXPECT_EQ(PatcherMeshShaderVanillaParallax::shaderType(), PGEnums::ShapeShader::VanillaParallax);
    EXPECT_EQ(patcher.patcherName(), "VanillaParallax");

    EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));
    EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::Grass));
    EXPECT_FALSE(patcher.canApply(*m_shape, true, ModelRecordType::StaticObject)); // single pass material

    // Only the default, parallax and environment map shader types are converted.
    auto* shader = m_builder.lightingShader(m_shape);
    for (const auto type : { nifly::BSLSP_DEFAULT, nifly::BSLSP_PARALLAX, nifly::BSLSP_ENVMAP }) {
        shader->SetShaderType(type);
        EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << type;
    }
    for (const auto type : {
             nifly::BSLSP_GLOWMAP,
             nifly::BSLSP_SKINTINT,
             nifly::BSLSP_HAIRTINT,
             nifly::BSLSP_MULTILAYERPARALLAX,
             nifly::BSLSP_EYE,
             nifly::BSLSP_FACE,
         }) {
        shader->SetShaderType(type);
        EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << type;
    }
    shader->SetShaderType(nifly::BSLSP_DEFAULT);

    // Decals and mesh lighting do not work with parallax.
    for (const auto flag : { nifly::SLSF1_DECAL, nifly::SLSF1_DYNAMIC_DECAL }) {
        PGNIFUtil::setShaderFlag(shader, flag);
        EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << flag;
        PGNIFUtil::clearShaderFlag(shader, flag);
    }
    for (const auto flag : {
             nifly::SLSF2_SOFT_LIGHTING,
             nifly::SLSF2_RIM_LIGHTING,
             nifly::SLSF2_BACK_LIGHTING,
             nifly::SLSF2_ANISOTROPIC_LIGHTING,
         }) {
        PGNIFUtil::setShaderFlag(shader, flag);
        EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << flag;
        PGNIFUtil::clearShaderFlag(shader, flag);
    }
    EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));

    // Alpha blended, skinned and effect shader shapes are not converted either.
    auto* alpha = m_builder.addShape("Alpha", slots(L"textures\\a\\stone.dds"));
    m_builder.addAlphaProperty(alpha);
    EXPECT_FALSE(patcher.canApply(*alpha, false, ModelRecordType::StaticObject));
    auto* skinned = m_builder.addShape("Skinned", slots(L"textures\\a\\stone.dds"));
    skinned->SetSkinned(true);
    EXPECT_FALSE(patcher.canApply(*skinned, false, ModelRecordType::StaticObject));
    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.canApply(*effect, false, ModelRecordType::StaticObject));
}

TEST_F(PatcherMeshShaderVanillaParallaxTest,
       AttachedHavokDisablesParallaxForTheWholeMesh)
{
    auto& nif = m_builder.nif();
    nif.AssignExtraData(m_builder.root(), std::make_unique<nifly::BSBehaviorGraphExtraData>());

    // The check happens when the patcher is created.
    PatcherMeshShaderVanillaParallax patcher(meshPath, &nif);
    EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));

    // Without a mesh there is nothing to look at until one is needed.
    PatcherMeshShaderVanillaParallax noMesh(meshPath, nullptr);
    EXPECT_THROW(noMesh.canApply(*m_shape, false, ModelRecordType::StaticObject), std::runtime_error);
}

TEST_F(PatcherMeshShaderVanillaParallaxTest,
       ShouldApplyFindsHeightMapsWithTheSameAspectRatio)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderVanillaParallax patcher(meshPath, &nif);

    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*m_shape, matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_p.dds");
    EXPECT_FALSE(matches[0].extraData);
    EXPECT_EQ(patcher.matchExtraDataHash(matches[0]), 0U);

    // The normal map is searched first, then the diffuse, and either is enough.
    EXPECT_TRUE(patcher.shouldApply(slots(L"textures\\a\\stone.dds"), matches));
    EXPECT_EQ(matches.size(), 1U);
    EXPECT_TRUE(patcher.shouldApply(slots(L"", L"textures\\a\\stone_n.dds"), matches));
    EXPECT_EQ(matches.size(), 1U);

    // Textures that do not exist have no height map, and neither does an empty set.
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\missing.dds", L"textures\\a\\missing_n.dds"), matches));
    EXPECT_TRUE(matches.empty());
    EXPECT_FALSE(patcher.shouldApply(PGTypes::TextureSet { }, matches));

    // A height map with another aspect ratio than the texture is not a match.
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\wide.dds", L"textures\\a\\wide_n.dds"), matches));
    EXPECT_TRUE(matches.empty());
}

TEST_F(PatcherMeshShaderVanillaParallaxTest,
       TheHeightMapAlreadyInUseIsOrderedLast)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderVanillaParallax patcher(meshPath, &nif);

    // A second height map for the same base, as a manual mapping would add it.
    m_pgd->textureMap(TextureSlots::Parallax)[L"textures\\a\\stone"].insert(
        { .path = L"textures\\a\\alt_p.dds", .type = TextureType::Height });

    auto current = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    current[3] = L"textures\\a\\stone_p.dds";
    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(current, matches));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\alt_p.dds");
    EXPECT_EQ(matches[1].matchedPath, L"textures\\a\\stone_p.dds");

    current[3] = L"textures\\a\\alt_p.dds";
    EXPECT_TRUE(patcher.shouldApply(current, matches));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_p.dds");
    EXPECT_EQ(matches[1].matchedPath, L"textures\\a\\alt_p.dds");
}

TEST_F(PatcherMeshShaderVanillaParallaxTest,
       ApplyPatchSetsTheParallaxShaderAndTheHeightMapSlot)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderVanillaParallax patcher(meshPath, &nif);
    auto* shader = m_builder.lightingShader(m_shape);
    shader->SetShaderType(nifly::BSLSP_ENVMAP);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_UNUSED01);
    ASSERT_FALSE(m_shape->HasVertexColors());

    PatcherMatch match;
    match.matchedPath = L"textures\\a\\stone_p.dds";
    auto patched = PGNIFUtil::textureSlots(&nif, m_shape);
    patcher.applyPatch(patched, *m_shape, match);

    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_PARALLAX);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_UNUSED01));
    // Parallax needs vertex colors on the geometry and the shader.
    EXPECT_TRUE(m_shape->HasVertexColors());
    EXPECT_TRUE(shader->HasVertexColors());
    EXPECT_EQ(patched[3], L"textures\\a\\stone_p.dds");
    EXPECT_EQ(patched[0], L"textures\\a\\stone.dds");
    EXPECT_EQ(patched[1], L"textures\\a\\stone_n.dds");

    // The slots alone, and applying the shader again, give the same result.
    PGTypes::TextureSet slotsOnly;
    patcher.applyPatchSlots(slotsOnly, match);
    EXPECT_EQ(slotsOnly[3], L"textures\\a\\stone_p.dds");
    EXPECT_TRUE(slotsOnly[0].empty());
    patcher.applyShader(*m_shape);
    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_PARALLAX);

    // Shapes without a lighting shader are left alone.
    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_NO_THROW(patcher.applyShader(*effect));

    const auto fromFactory = PatcherMeshShaderVanillaParallax::factory()(meshPath, &nif);
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "VanillaParallax");
}

}
