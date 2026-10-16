#include "patchers/PatcherMeshShaderComplexMaterial.hpp"

#include "patchers/PatcherTextureHookConvertToCM.hpp"

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

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureAttribute;
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
 * @brief A data folder with complex material textures (mapped manually, since their classification needs the GPU),
 * one of them with a material JSON, mapped by the global PGDirectory with a global PGD3D for the aspect ratio checks.
 */
class PatcherMeshShaderComplexMaterialTest : public ::testing::Test {
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
                 L"textures\\a\\stone_m.dds",
                 L"textures\\a\\metal.dds",
                 L"textures\\a\\metal_m.dds",
                 L"textures\\a\\bad.dds",
                 L"textures\\a\\bad_m.dds",
                 L"textures\\a\\wide_m.dds",
                 L"textures\\a\\rock.dds",
             }) {
            TestUtil::DDSBuilder::writeDDS(dataDir() / texture, 64, 64);
        }
        TestUtil::DDSBuilder::writeDDS(dataDir() / L"textures\\a\\wide.dds", 128, 64);
        TestUtil::writeFile(dataDir() / L"textures\\a\\stone_m.json",
                            R"({"specular_enabled": false, "specular_color": [0.5, 0.25, 1], "specular_strength": 2,
                                "glossiness": 50, "environment_map_scale": 0.5, "dynamic_cubemap": false})");
        TestUtil::writeFile(dataDir() / L"textures\\a\\bad_m.json", "{not json");

        m_shape = m_builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        m_builder.save(dataDir() / meshPath);

        m_pgd = std::make_unique<PGDirectory>(dataDir(), m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        m_pgd->mapFiles({ },
                        { },
                        {
                            { L"textures\\a\\stone_m.dds", TextureType::ComplexMaterial },
                            { L"textures\\a\\metal_m.dds", TextureType::ComplexMaterial },
                            { L"textures\\a\\bad_m.dds", TextureType::ComplexMaterial },
                            { L"textures\\a\\wide_m.dds", TextureType::ComplexMaterial },
                        },
                        { },
                        false);
        m_pgd->waitForMeshMapping();
        m_pgd->addTextureAttribute(L"textures\\a\\metal_m.dds", TextureAttribute::CMMetalness);
        m_pgd->addTextureAttribute(L"textures\\a\\metal_m.dds", TextureAttribute::CMGlossiness);
        PGGlobals::setPGD(m_pgd.get());
        PGGlobals::setPGD3D(&m_d3d);
    }

    void TearDown() override
    {
        PatcherMeshShaderComplexMaterial::loadOptions(false);
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
    }

    [[nodiscard]] static PatcherMatch matchFor(const std::wstring& envMask)
    {
        PatcherMatch match;
        match.matchedPath = envMask;
        return match;
    }
};

TEST_F(PatcherMeshShaderComplexMaterialTest,
       CanApplyRejectsIncompatibleShaders)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);
    EXPECT_EQ(PatcherMeshShaderComplexMaterial::shaderType(), PGEnums::ShapeShader::ComplexMaterial);
    EXPECT_EQ(patcher.patcherName(), "ComplexMaterial");

    EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));
    EXPECT_TRUE(patcher.canApply(*m_shape, true, ModelRecordType::StaticObject));
    EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::Grass));

    auto* shader = m_builder.lightingShader(m_shape);
    for (const auto type : { nifly::BSLSP_DEFAULT, nifly::BSLSP_ENVMAP, nifly::BSLSP_PARALLAX }) {
        shader->SetShaderType(type);
        EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << type;
    }
    for (const auto type : { nifly::BSLSP_GLOWMAP, nifly::BSLSP_SKINTINT, nifly::BSLSP_MULTILAYERPARALLAX }) {
        shader->SetShaderType(type);
        EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << type;
    }
    shader->SetShaderType(nifly::BSLSP_DEFAULT);

    // Mesh lighting is fine on its own, but not together with anisotropic lighting, not all three at once and not on a
    // single pass material.
    for (const auto flag : { nifly::SLSF2_SOFT_LIGHTING, nifly::SLSF2_RIM_LIGHTING, nifly::SLSF2_BACK_LIGHTING }) {
        PGNIFUtil::setShaderFlag(shader, flag);
        EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << flag;
        EXPECT_FALSE(patcher.canApply(*m_shape, true, ModelRecordType::StaticObject)) << flag;
        PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_ANISOTROPIC_LIGHTING);
        EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject)) << flag;
        PGNIFUtil::clearShaderFlag(shader, nifly::SLSF2_ANISOTROPIC_LIGHTING);
        PGNIFUtil::clearShaderFlag(shader, flag);
    }
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_ANISOTROPIC_LIGHTING);
    EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));
    PGNIFUtil::clearShaderFlag(shader, nifly::SLSF2_ANISOTROPIC_LIGHTING);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_SOFT_LIGHTING);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_RIM_LIGHTING);
    EXPECT_TRUE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_BACK_LIGHTING);
    EXPECT_FALSE(patcher.canApply(*m_shape, false, ModelRecordType::StaticObject));

    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_FALSE(patcher.canApply(*effect, false, ModelRecordType::StaticObject));
}

TEST_F(PatcherMeshShaderComplexMaterialTest,
       ShouldApplyFindsComplexMaterialMapsAndTheirMetadata)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);

    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*m_shape, matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_m.dds");
    ASSERT_TRUE(matches[0].extraData);
    const auto meta = *std::static_pointer_cast<nlohmann::json>(matches[0].extraData);
    EXPECT_EQ(meta["glossiness"], 50);
    const auto hash = patcher.matchExtraDataHash(matches[0]);
    EXPECT_NE(hash, 0U);
    EXPECT_EQ(patcher.matchExtraDataHash(matches[0]), hash);

    // Without a JSON there is no extra data.
    EXPECT_TRUE(patcher.shouldApply(slots(L"textures\\a\\metal.dds"), matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\metal_m.dds");
    EXPECT_FALSE(matches[0].extraData);
    EXPECT_EQ(patcher.matchExtraDataHash(matches[0]), 0U);

    // An unreadable JSON is reported and ignored.
    EXPECT_TRUE(patcher.shouldApply(slots(L"textures\\a\\bad.dds"), matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_FALSE(matches[0].extraData);
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Failed to parse JSON: textures\\a\\bad_m.json"));

    // Textures that do not exist and aspect ratio mismatches give no match.
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\missing.dds"), matches));
    EXPECT_TRUE(matches.empty());
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\wide.dds"), matches));
    EXPECT_FALSE(patcher.shouldApply(PGTypes::TextureSet { }, matches));

    // The map already in the environment mask slot is ordered last.
    m_pgd->textureMap(TextureSlots::EnvMask)[L"textures\\a\\stone"].insert(
        { .path = L"textures\\a\\metal_m.dds", .type = TextureType::ComplexMaterial });
    auto current = slots(L"textures\\a\\stone.dds");
    current[5] = L"textures\\a\\stone_m.dds";
    EXPECT_TRUE(patcher.shouldApply(current, matches));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\metal_m.dds");
    EXPECT_EQ(matches[1].matchedPath, L"textures\\a\\stone_m.dds");
}

TEST_F(PatcherMeshShaderComplexMaterialTest,
       GeneratedMapsAreNotDirectCandidates)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);
    std::vector<PatcherMatch> matches;

    // A map the Parallax to CM hook generates is reached through that transform, never as a direct match: replaying
    // it records its type but adds nothing to the texture maps.
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\rock.dds"), matches));
    PatcherTextureHookConvertToCM::replayGenerated(L"textures\\a\\rock_p.dds");
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\rock.dds"), matches));
    EXPECT_TRUE(matches.empty());
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\rock_m.dds"), TextureType::ComplexMaterial);

    // A mapped map stays a candidate once a hook output took over its path, so the recorded matches do not depend on
    // the order in which the meshes are patched.
    TestUtil::DDSBuilder::writeDDS(m_dir / L"Output\\textures\\a\\stone_m.dds", 64, 64);
    m_pgd->addGeneratedFile(L"textures\\a\\stone_m.dds");
    EXPECT_TRUE(patcher.shouldApply(slots(L"textures\\a\\stone.dds"), matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_m.dds");
}

TEST_F(PatcherMeshShaderComplexMaterialTest,
       ApplyShaderTurnsTheShapeIntoAnEnvironmentMap)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);
    auto* shader = m_builder.lightingShader(m_shape);
    shader->SetShaderType(nifly::BSLSP_PARALLAX);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF1_PARALLAX);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_UNUSED01);
    PGNIFUtil::setShaderFlag(shader, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    shader->environmentMapScale = 0.25F;
    shader->specularStrength = 3;

    patcher.applyShader(*m_shape);
    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_ENVMAP);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_UNUSED01));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_FLOAT_EQ(shader->environmentMapScale, 1);
    EXPECT_FLOAT_EQ(shader->specularStrength, 1);

    auto* effect = m_builder.addEffectShape("Glow", "textures\\a\\stone.dds");
    EXPECT_NO_THROW(patcher.applyShader(*effect));
    PGTypes::TextureSet effectSlots;
    EXPECT_NO_THROW(patcher.applyPatch(effectSlots, *effect, matchFor(L"textures\\a\\stone_m.dds")));
}

TEST_F(PatcherMeshShaderComplexMaterialTest,
       ApplyPatchUsesTextureAttributesAndMetadata)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);
    auto* shader = m_builder.lightingShader(m_shape);

    // A metallic, glossy map turns the specular color white and enables specular.
    PGNIFUtil::clearShaderFlag(shader, nifly::SLSF1_SPECULAR);
    shader->specularColor = nifly::Vector3(0.2F, 0.4F, 0.6F);
    auto patched = slots(L"textures\\a\\metal.dds");
    patched[3] = L"textures\\a\\metal_p.dds";
    patcher.applyPatch(patched, *m_shape, matchFor(L"textures\\a\\metal_m.dds"));
    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_ENVMAP);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_SPECULAR));
    EXPECT_FLOAT_EQ(shader->specularColor.x, 1);
    EXPECT_FLOAT_EQ(shader->specularColor.y, 1);
    EXPECT_FLOAT_EQ(shader->specularColor.z, 1);
    // The height map slot is cleared, the mask and the dynamic cubemap are set.
    EXPECT_TRUE(patched[3].empty());
    EXPECT_EQ(patched[5], L"textures\\a\\metal_m.dds");
    EXPECT_EQ(patched[4], PatcherMeshShaderComplexMaterial::s_dynCubemapPath.wstring());
    EXPECT_EQ(patched[0], L"textures\\a\\metal.dds");

    // The material JSON overrides the shader values and can disable the dynamic cubemap.
    std::vector<PatcherMatch> matches;
    ASSERT_TRUE(patcher.shouldApply(slots(L"textures\\a\\stone.dds"), matches));
    ASSERT_EQ(matches.size(), 1U);
    auto metaPatched = slots(L"textures\\a\\stone.dds");
    metaPatched[4] = L"textures\\cubemaps\\old_e.dds";
    patcher.applyPatch(metaPatched, *m_shape, matches[0]);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(shader, nifly::SLSF1_SPECULAR));
    EXPECT_FLOAT_EQ(shader->specularColor.x, 0.5F);
    EXPECT_FLOAT_EQ(shader->specularColor.y, 0.25F);
    EXPECT_FLOAT_EQ(shader->specularColor.z, 1);
    EXPECT_FLOAT_EQ(shader->specularStrength, 2);
    EXPECT_FLOAT_EQ(shader->glossiness, 50);
    EXPECT_FLOAT_EQ(shader->environmentMapScale, 0.5F);
    EXPECT_EQ(metaPatched[5], L"textures\\a\\stone_m.dds");
    EXPECT_EQ(metaPatched[4], L"textures\\cubemaps\\old_e.dds"); // dynamic cubemaps disabled by the JSON

    // The option disables dynamic cubemaps for every match.
    PatcherMeshShaderComplexMaterial::loadOptions(true);
    auto noCubemap = slots(L"textures\\a\\metal.dds");
    noCubemap[4] = L"textures\\cubemaps\\old_e.dds";
    patcher.applyPatchSlots(noCubemap, matchFor(L"textures\\a\\metal_m.dds"));
    EXPECT_EQ(noCubemap[4], L"textures\\cubemaps\\old_e.dds");
    EXPECT_EQ(noCubemap[5], L"textures\\a\\metal_m.dds");
    PatcherMeshShaderComplexMaterial::loadOptions(false);
    patcher.applyPatchSlots(noCubemap, matchFor(L"textures\\a\\metal_m.dds"));
    EXPECT_EQ(noCubemap[4], PatcherMeshShaderComplexMaterial::s_dynCubemapPath.wstring());

    // The string options do the same.
    std::unordered_map<std::string, std::string> options { { "disable_dyncubemap", "" } };
    PatcherMeshShaderComplexMaterial::loadOptions(options);
    noCubemap[4].clear();
    patcher.applyPatchSlots(noCubemap, matchFor(L"textures\\a\\metal_m.dds"));
    EXPECT_TRUE(noCubemap[4].empty());

    const auto fromFactory = PatcherMeshShaderComplexMaterial::factory()(meshPath, &nif);
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "ComplexMaterial");
}

TEST_F(PatcherMeshShaderComplexMaterialTest,
       ApplyPatchIgnoresMalformedSpecularColor)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderComplexMaterial patcher(meshPath, &nif);
    auto* shader = m_builder.lightingShader(m_shape);

    // A specular color that is not three numbers is ignored like any other key of the wrong type, and the rest of the
    // material JSON still applies.
    for (const auto* specularColor : {
             R"([1, "1", 1])",
             "[1, null, 1]",
             "[1, [1], 1]",
             R"([1, {"g": 1}, 1])",
             "[1, true, 1]",
             "[1, 1]",
             "[1, 1, 1, 1]",
             R"("white")",
         }) {
        shader->specularColor = nifly::Vector3(0.2F, 0.4F, 0.6F);
        shader->glossiness = 10;

        auto meta = nlohmann::json::parse(R"({"glossiness": 30})");
        meta["specular_color"] = nlohmann::json::parse(specularColor);
        auto match = matchFor(L"textures\\a\\stone_m.dds");
        match.extraData = std::make_shared<nlohmann::json>(meta);

        auto patched = slots(L"textures\\a\\stone.dds");
        EXPECT_NO_THROW(patcher.applyPatch(patched, *m_shape, match)) << specularColor;
        EXPECT_FLOAT_EQ(shader->specularColor.x, 0.2F) << specularColor;
        EXPECT_FLOAT_EQ(shader->specularColor.y, 0.4F) << specularColor;
        EXPECT_FLOAT_EQ(shader->specularColor.z, 0.6F) << specularColor;
        EXPECT_FLOAT_EQ(shader->glossiness, 30) << specularColor;
        EXPECT_EQ(patched[5], L"textures\\a\\stone_m.dds") << specularColor;
    }
}

}
