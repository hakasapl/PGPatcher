#include "patchers/PatcherMeshShaderTruePBR.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
#include "patchers/PatcherMeshPreConvertLegacyPBR.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureSlots;
using PGEnums::TextureType;
using PatcherMatch = PatcherMeshShader::PatcherMatch;
using ModelRecordType = PGPlugin::ModelRecordType;
using TestUtil::NIFBuilder;
using ExtraData = std::map<size_t, std::tuple<nlohmann::json, std::wstring>>;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    return set;
}

/// @brief The entries behind a match, in application order.
std::vector<nlohmann::json> entriesOf(const PatcherMatch& match)
{
    std::vector<nlohmann::json> entries;
    if (!match.extraData)
        return entries;
    for (const auto& [sequence, data] : *std::static_pointer_cast<ExtraData>(match.extraData))
        entries.push_back(std::get<0>(data));
    return entries;
}

/**
 * @brief A data folder with PBR configs covering every way an entry can match, the PBR textures they point to and a
 * few vanilla textures, mapped by the global PGDirectory.
 */
class PatcherMeshShaderTruePBRTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    NIFBuilder m_builder;
    nifly::BSTriShape* m_shape = nullptr;
    nifly::BSLightingShaderProperty* m_shader = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\clutter\\stone.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }

    void SetUp() override
    {
        // Vanilla textures and the PBR textures the entries resolve to.
        for (const auto* texture : {
                 L"textures\\a\\stone.dds",
                 L"textures\\a\\stone_n.dds",
                 L"textures\\a\\wood.dds",
                 L"textures\\a\\wood_n.dds",
                 L"textures\\a\\cobble01.dds",
                 L"textures\\a\\exact.dds",
                 L"textures\\a\\fuzzy.dds",
                 L"textures\\a\\rmaos.dds",
                 L"textures\\a\\rmaos_rmaos.dds",
                 L"textures\\a\\keep_rmaos.dds",
                 L"textures\\c\\metal.dds",
                 L"textures\\c\\cloth.dds",
                 L"textures\\a\\missing.dds",
                 L"textures\\pbr\\a\\stone.dds",
                 L"textures\\pbr\\a\\stone_n.dds",
                 L"textures\\pbr\\a\\stone_rmaos.dds",
                 L"textures\\pbr\\a\\stone_g.dds",
                 L"textures\\pbr\\a\\stone_p.dds",
                 L"textures\\pbr\\a\\stone_s.dds",
                 L"textures\\pbr\\a\\oak_n.dds",
                 L"textures\\pbr\\a\\oak_rmaos.dds",
                 L"textures\\pbr\\a\\cobble01.dds",
                 L"textures\\pbr\\a\\cobble01_n.dds",
                 L"textures\\pbr\\a\\cobble01_rmaos.dds",
                 L"textures\\pbr\\a\\cobble01_cnr.dds",
                 L"textures\\pbr\\a\\cobble01_s.dds",
                 L"textures\\pbr\\a\\exact.dds",
                 L"textures\\pbr\\a\\exact_n.dds",
                 L"textures\\pbr\\a\\exact_rmaos.dds",
                 L"textures\\pbr\\a\\fuzzy.dds",
                 L"textures\\pbr\\a\\fuzzy_n.dds",
                 L"textures\\pbr\\a\\fuzzy_f.dds",
                 L"textures\\custom\\sub_s.dds",
                 L"textures\\pbr\\c\\metal.dds",
                 L"textures\\pbr\\c\\metal_n.dds",
                 L"textures\\pbr\\c\\metal_rmaos.dds",
                 L"textures\\pbr\\c\\cloth.dds",
                 L"textures\\pbr\\c\\cloth_n.dds",
                 L"textures\\pbr\\c\\cloth_rmaos.dds",
             }) {
            TestUtil::writeFile(dataDir() / texture, "DDS");
        }

        // An array config exercising the matching rules and most attributes.
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\a.json", R"([
  {"texture": "a\\stone", "emissive": true, "parallax": true, "subsurface": true, "specular_level": 0.04,
   "roughness_scale": 1.5, "subsurface_color": [1, 0.5, 0.25], "subsurface_opacity": 0.8, "displacement_scale": 0.3,
   "zbuffer_write": false, "vertex_colors": true, "uv_scale": 2, "emissive_scale": 3,
   "emissive_color": [1, 0.5, 0.25, 1], "smooth_angle": false},
  {"match_normal": "a\\wood", "rename": "a\\oak", "lock_diffuse": true, "nif_filter": "furniture"},
  {"match_diffuse": "b\\brick", "delete": true},
  {"path_contains": "cobble", "multilayer": true, "coat_color": [0.1, 0.2, 0.3], "coat_specular_level": 0.5,
   "coat_roughness": 0.6, "coat_strength": 0.7, "coat_diffuse": true, "coat_parallax": true, "coat_normal": true,
   "inner_uv_scale": 4},
  {"match1": "a\\exact.dds", "glint": {"screen_space_scale": 1.5, "log_microfacet_density": 2.5,
   "microfacet_roughness": 0.3, "density_randomization": 0.4}},
  {"texture": "a\\fuzzy", "fuzz": {"color": [0.1, 0.2, 0.3], "weight": 0.9, "texture": true}, "hair": true,
   "lock_rmaos": true, "slot8": "custom\\sub_s.dds"},
  {"texture": "a\\missing"}
])");
        // An object config whose defaults fill in the entries.
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\c.json",
                            R"({"default": {"specular_level": 0.1, "parallax": false},
    "entries": [{"texture": "c\\metal", "specular_level": 0.2}, {"texture": "c\\cloth"}]})");
        // A second entry for the stone from another config, an object without entries and a broken file.
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\extra.json",
                            R"([{"texture": "a\\stone", "specular_level": 0.5}])");
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\odd.json", R"({"something": 1})");
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\broken.json", "[{");

        m_shape = m_builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        m_shader = m_builder.lightingShader(m_shape);
        m_builder.save(dataDir() / meshPath);

        m_pgd = std::make_unique<PGDirectory>(dataDir(), m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        // The file mapping lists the PBR configs and classifies the textures.
        m_pgd->mapFiles({ }, { }, { }, { }, false);
        m_pgd->waitForMeshMapping();
        m_pgd->setTextureType(L"textures\\a\\rmaos_rmaos.dds", TextureType::RMAOS);
        PGGlobals::setPGD(m_pgd.get());

        PatcherMeshShaderTruePBR::loadOptions(true, false);
        PatcherMeshShaderTruePBR::loadStatics(m_pgd->pbrJSONs(), false);
        PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Legacy);
    }

    void TearDown() override
    {
        PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Standalone);
        PatcherMeshShaderTruePBR::loadOptions(true, false);
        PGGlobals::setPGD(nullptr);
    }

    [[nodiscard]] static std::vector<PatcherMatch> matchesFor(PatcherMeshShaderTruePBR& patcher,
                                                              const PGTypes::TextureSet& oldSlots)
    {
        std::vector<PatcherMatch> matches;
        patcher.shouldApply(oldSlots, matches);
        return matches;
    }
};

TEST_F(PatcherMeshShaderTruePBRTest,
       LoadStaticsReadsEveryConfigAndReportsBrokenOnes)
{
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Failed to parse JSON: pbrnifpatcher\\broken.json"));

    // 7 entries from a.json, 2 from c.json, 1 from extra.json; odd.json has no entries.
    const auto& configs = PatcherMeshShaderTruePBR::truePBRConfigs();
    EXPECT_EQ(configs.size(), 10U);
    EXPECT_EQ(configs.at(0)["match_diffuse"], "\\a\\stone"); // "texture" is an alias with the separator added
    EXPECT_EQ(configs.at(0)["json"], "pbrnifpatcher\\a.json");
    EXPECT_EQ(configs.at(1)["rename"], "\\a\\oak");
    EXPECT_EQ(configs.at(7)["specular_level"], 0.2); // its own value wins over the default
    EXPECT_EQ(configs.at(7)["parallax"], false); // the default fills in the rest
    EXPECT_EQ(configs.at(8)["specular_level"], 0.1);
    EXPECT_EQ(configs.at(9)["json"], "pbrnifpatcher\\extra.json");

    EXPECT_EQ(PatcherMeshShaderTruePBR::pathLookupJSONs().size(), 1U);
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRNormalInverse().size(), 1U);
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRDiffuseInverse().at(L"enots\\a\\"),
              (std::vector<size_t> { 0, 9 })); // keys are the reversed paths
    EXPECT_TRUE(
        PatcherMeshShaderTruePBR::truePBRMatchXMap().at(TextureSlots::Diffuse).contains(L"textures\\a\\exact.dds"));
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRConfigFilenameFields(),
              (std::vector<std::string> { "match_normal", "match_diffuse", "rename" }));

    // Loading again replaces the configs instead of adding to them.
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\extra.json" }, false);
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRConfigs().size(), 1U);
    EXPECT_TRUE(PatcherMeshShaderTruePBR::pathLookupJSONs().empty());
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRDiffuseInverse().at(L"enots\\a\\"), (std::vector<size_t> { 0 }));
    EXPECT_EQ(PatcherMeshShaderTruePBR::shaderType(), PGEnums::ShapeShader::TruePBR);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       EntriesMatchByDiffuseNormalPathAndSlot)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    EXPECT_TRUE(patcher.canApply(*m_shape, true, ModelRecordType::Grass)); // PBR applies to anything

    // The diffuse of the stone matches two configs, one match per config file, the later file first.
    auto matches = matchesFor(patcher, slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, L"pbrnifpatcher\\extra.json");
    EXPECT_EQ(matches[1].matchedPath, L"pbrnifpatcher\\a.json");
    ASSERT_EQ(entriesOf(matches[1]).size(), 1U);
    EXPECT_EQ(entriesOf(matches[1])[0]["match_diffuse"], "\\a\\stone");
    EXPECT_EQ(std::get<1>((*std::static_pointer_cast<ExtraData>(matches[1].extraData)).begin()->second),
              L"textures\\pbr\\a\\stone");
    const auto hash = patcher.matchExtraDataHash(matches[1]);
    EXPECT_NE(hash, 0U);
    EXPECT_EQ(patcher.matchExtraDataHash(matches[1]), hash);
    EXPECT_NE(patcher.matchExtraDataHash(matches[0]), hash);
    EXPECT_EQ(patcher.matchExtraDataHash(PatcherMatch { }), 0U);

    // A mesh that already points into textures\pbr matches as if it did not.
    matches = matchesFor(patcher, slots(L"textures\\pbr\\a\\stone.dds", L"textures\\pbr\\a\\stone_n.dds"));
    EXPECT_EQ(matches.size(), 2U);

    // The normal entry only applies to furniture meshes, unless the mesh is facegen or BodySlide data.
    const auto wood = slots(L"textures\\a\\wood.dds", L"textures\\a\\wood_n.dds");
    EXPECT_TRUE(matchesFor(patcher, wood).empty());
    PatcherMeshShaderTruePBR furniture(L"meshes\\furniture\\chair.nif", &m_builder.nif());
    matches = matchesFor(furniture, wood);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(std::get<1>(std::static_pointer_cast<ExtraData>(matches[0].extraData)->begin()->second),
              L"textures\\pbr\\a\\oak"); // renamed
    PGPlugin::MeshUseAttributes facegen;
    facegen.isFacegen = true;
    patcher.setMeshUse(facegen);
    EXPECT_EQ(matchesFor(patcher, wood).size(), 1U);
    patcher.setMeshUse(PGPlugin::MeshUseAttributes { });

    // Path contains and exact slot matches.
    EXPECT_EQ(matchesFor(patcher, slots(L"textures\\a\\cobble01.dds")).size(), 1U);
    EXPECT_EQ(matchesFor(patcher, slots(L"textures\\a\\exact.dds")).size(), 1U);
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\a\\exactly.dds")).empty());

    // An entry whose textures do not exist is dropped, unless path checks are off.
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\a\\missing.dds")).empty());
    std::unordered_map<std::string, std::string> options { { "no_path_check", "" }, { "print_nonexistent_paths", "" } };
    PatcherMeshShaderTruePBR::loadOptions(options);
    EXPECT_EQ(matchesFor(patcher, slots(L"textures\\a\\missing.dds")).size(), 1U);
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn,
                                  "Texture \"textures\\pbr\\a\\missing.dds\" does not exist from PBR json "
                                  "\"pbrnifpatcher\\a.json\" when patching mesh \"meshes\\clutter\\stone.nif\""));
    PatcherMeshShaderTruePBR::loadOptions(true, false);
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\a\\missing.dds")).empty());

    // Nothing matches nothing.
    EXPECT_TRUE(matchesFor(patcher, PGTypes::TextureSet { }).empty());
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\a\\other.dds", L"textures\\a\\other_n.dds")).empty());
}

TEST_F(PatcherMeshShaderTruePBRTest,
       PrePatchedShapesMatchWithoutAConfig)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());

    // A shape whose environment mask slot holds an RMAOS texture is already PBR.
    auto prePatched = slots(L"textures\\a\\rmaos.dds");
    prePatched[5] = L"textures\\a\\rmaos_rmaos.dds";
    auto matches = matchesFor(patcher, prePatched);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\rmaos_rmaos.dds");
    EXPECT_FALSE(matches[0].extraData);

    // Applying such a match changes nothing.
    auto applied = prePatched;
    patcher.applyPatch(applied, *m_shape, matches[0]);
    patcher.applyPatchSlots(applied, matches[0]);
    EXPECT_EQ(applied, prePatched);

    // Through the shape, the PBR flag with an existing RMAOS counts as well.
    m_builder.setTextures(m_shape, prePatched);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_UNUSED01);
    EXPECT_TRUE(patcher.shouldApply(*m_shape, matches));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, meshPath);
    EXPECT_FALSE(matches[0].extraData);

    // Shapes without a lighting shader are no problem (nifly reports the source texture of an effect shader as the
    // diffuse slot, so one that matches no entry is used here).
    auto* effect = m_builder.addEffectShape("Glow", "textures\\fx\\glow.dds");
    EXPECT_FALSE(patcher.shouldApply(*effect, matches));
    EXPECT_NO_THROW(patcher.applyShader(*effect));
}

TEST_F(PatcherMeshShaderTruePBRTest,
       ApplyPatchSetsTheSlotsFlagsAndFieldsOfTheEntries)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->SetShaderType(nifly::BSLSP_ENVMAP);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_GLOW_MAP);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_ZBUFFER_WRITE);
    ASSERT_FALSE(m_shape->HasVertexColors());

    auto matches = matchesFor(patcher, slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    ASSERT_EQ(matches.size(), 2U);
    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    patched[4] = L"textures\\cubemaps\\c_e.dds";
    patcher.applyPatch(patched, *m_shape, matches[1]); // the a.json entry

    EXPECT_EQ(patched[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(patched[1], L"textures\\pbr\\a\\stone_n.dds");
    EXPECT_EQ(patched[2], L"textures\\pbr\\a\\stone_g.dds"); // emissive
    EXPECT_EQ(patched[3], L"textures\\pbr\\a\\stone_p.dds"); // parallax
    EXPECT_TRUE(patched[4].empty()); // no cubemap for PBR
    EXPECT_EQ(patched[5], L"textures\\pbr\\a\\stone_rmaos.dds");
    EXPECT_TRUE(patched[6].empty());
    EXPECT_EQ(patched[7], L"textures\\pbr\\a\\stone_s.dds"); // subsurface

    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_EXTERNAL_EMITTANCE));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING)); // subsurface
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_GLOW_MAP));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_ZBUFFER_WRITE));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING));
    EXPECT_TRUE(m_shape->HasVertexColors());
    EXPECT_TRUE(m_shader->HasVertexColors());
    EXPECT_FLOAT_EQ(m_shader->glossiness, 0.04F); // specular level
    EXPECT_FLOAT_EQ(m_shader->specularStrength, 1.5F); // roughness scale
    EXPECT_EQ(m_shader->specularColor, nifly::Vector3(1, 0.5F, 0.25F)); // subsurface color
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.8F); // subsurface opacity
    EXPECT_FLOAT_EQ(m_shader->rimlightPower, 0.3F); // displacement scale
    EXPECT_FLOAT_EQ(m_shader->emissiveMultiple, 3);
    EXPECT_EQ(m_shader->emissiveColor, nifly::Vector3(1, 0.5F, 0.25F));
    EXPECT_FLOAT_EQ(m_shader->uvScale.u, 2);
    EXPECT_FLOAT_EQ(m_shader->uvScale.v, 2);

    // The slots alone give the same textures, and the other config only changes the specular level.
    PGTypes::TextureSet slotsOnly;
    patcher.applyPatchSlots(slotsOnly, matches[1]);
    EXPECT_EQ(slotsOnly[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(slotsOnly[7], L"textures\\pbr\\a\\stone_s.dds");
    patcher.applyPatch(patched, *m_shape, matches[0]);
    EXPECT_FLOAT_EQ(m_shader->glossiness, 0.5F);
    EXPECT_EQ(patched[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(patched[2], L"textures\\pbr\\a\\stone_g.dds"); // the second entry says nothing about emissive
    EXPECT_TRUE(patched[7].empty()); // but the subsurface slot follows the entry, which has no subsurface
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING));
}

TEST_F(PatcherMeshShaderTruePBRTest,
       LockedSlotsRenamesAndSlotOverridesAreHonored)
{
    PatcherMeshShaderTruePBR furniture(L"meshes\\furniture\\chair.nif", &m_builder.nif());
    auto wood = slots(L"textures\\a\\wood.dds", L"textures\\a\\wood_n.dds");
    auto matches = matchesFor(furniture, wood);
    ASSERT_EQ(matches.size(), 1U);
    furniture.applyPatchSlots(wood, matches[0]);
    EXPECT_EQ(wood[0], L"textures\\a\\wood.dds"); // locked
    EXPECT_EQ(wood[1], L"textures\\pbr\\a\\oak_n.dds"); // renamed
    EXPECT_EQ(wood[5], L"textures\\pbr\\a\\oak_rmaos.dds");

    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    auto fuzzy = slots(L"textures\\a\\fuzzy.dds");
    fuzzy[5] = L"textures\\a\\keep_rmaos.dds";
    matches = matchesFor(patcher, fuzzy);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(fuzzy, *m_shape, matches[0]);
    EXPECT_EQ(fuzzy[0], L"textures\\pbr\\a\\fuzzy.dds");
    EXPECT_EQ(fuzzy[5], L"textures\\a\\keep_rmaos.dds"); // lock_rmaos
    EXPECT_EQ(fuzzy[6], L"textures\\pbr\\a\\fuzzy_f.dds"); // fuzz texture
    EXPECT_EQ(fuzzy[7], L"textures\\custom\\sub_s.dds"); // slot8, with the textures folder added
    // Fuzz is a multilayer shader with soft lighting, hair adds back lighting.
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_MULTILAYERPARALLAX);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerThickness, 0.1F);
    EXPECT_FLOAT_EQ(m_shader->parallaxRefractionScale, 0.2F);
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.u, 0.3F);
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.v, 0.9F);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       MultilayerAndGlintEntriesSetTheirShaderParameters)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());

    auto cobble = slots(L"textures\\a\\cobble01.dds");
    auto matches = matchesFor(patcher, cobble);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(cobble, *m_shape, matches[0]);
    EXPECT_EQ(cobble[0], L"textures\\pbr\\a\\cobble01.dds");
    EXPECT_EQ(cobble[1], L"textures\\pbr\\a\\cobble01_n.dds");
    EXPECT_EQ(cobble[5], L"textures\\pbr\\a\\cobble01_rmaos.dds");
    EXPECT_EQ(cobble[6], L"textures\\pbr\\a\\cobble01_cnr.dds"); // coat normal
    EXPECT_EQ(cobble[7], L"textures\\pbr\\a\\cobble01_s.dds"); // coat diffuse
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_MULTILAYERPARALLAX);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_EFFECT_LIGHTING)); // coat diffuse
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING)); // coat parallax
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING)); // coat normal
    EXPECT_EQ(m_shader->specularColor, nifly::Vector3(0.1F, 0.2F, 0.3F)); // coat color
    EXPECT_FLOAT_EQ(m_shader->parallaxRefractionScale, 0.5F); // coat specular level
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerThickness, 0.6F); // coat roughness
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.7F); // coat strength
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.u, 4);
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.v, 4);

    auto exact = slots(L"textures\\a\\exact.dds");
    matches = matchesFor(patcher, exact);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(exact, *m_shape, matches[0]);
    EXPECT_EQ(exact[0], L"textures\\pbr\\a\\exact.dds");
    EXPECT_EQ(exact[5], L"textures\\pbr\\a\\exact_rmaos.dds");
    EXPECT_TRUE(exact[6].empty());
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_MULTILAYERPARALLAX);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING)); // no hair
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING)); // no fuzz
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerThickness, 1.5F);
    EXPECT_FLOAT_EQ(m_shader->parallaxRefractionScale, 2.5F);
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.u, 0.3F);
    EXPECT_FLOAT_EQ(m_shader->parallaxInnerLayerTextureScale.v, 0.4F);

    // A plain entry afterwards goes back to the default shader type.
    auto metal = slots(L"textures\\c\\metal.dds");
    matches = matchesFor(patcher, metal);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(metal, *m_shape, matches[0]);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_FLOAT_EQ(m_shader->glossiness, 0.2F);
    EXPECT_TRUE(metal[3].empty()); // parallax false from the defaults
}

TEST_F(PatcherMeshShaderTruePBRTest,
       DeleteEntriesRemoveTheShape)
{
    NIFBuilder builder;
    auto* brick = builder.addShape("Brick", slots(L"textures\\b\\brick.dds", L"textures\\b\\brick_n.dds"));
    builder.addShape("Keep", slots(L"textures\\a\\stone.dds"));
    PatcherMeshShaderTruePBR patcher(meshPath, &builder.nif());

    // The textures of a deleted shape are not checked.
    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*brick, matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(entriesOf(matches[0])[0]["delete"], true);

    auto brickSlots = PGNIFUtil::textureSlots(&builder.nif(), brick);
    patcher.applyPatch(brickSlots, *brick, matches[0]);
    EXPECT_EQ(builder.nif().GetBlockID(brick), nifly::NIF_NPOS);
    ASSERT_EQ(builder.nif().GetShapes().size(), 1U);
    EXPECT_EQ(builder.nif().GetShapes()[0]->name.get(), "Keep");
}

TEST_F(PatcherMeshShaderTruePBRTest,
       EntriesAfterADeleteEntryAreNotApplied)
{
    TestUtil::writeFile(
        dataDir() / L"pbrnifpatcher\\delete.json",
        R"([{"texture": "b\\brick", "delete": true}, {"path_contains": "b\\", "specular_level": 0.5}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\delete.json" }, false);

    // The shape shares its shader with another one, so the shader outlives the deleted shape.
    NIFBuilder builder;
    const auto brickTextures = slots(L"textures\\b\\brick.dds", L"textures\\b\\brick_n.dds");
    auto* keep = builder.addShape("Keep", brickTextures);
    auto* brick = builder.addShape("Brick", brickTextures);
    brick->ShaderPropertyRef()->index = keep->ShaderPropertyRef()->index;
    const auto* const sharedShader = builder.lightingShader(keep);
    const auto glossiness = sharedShader->glossiness;
    PatcherMeshShaderTruePBR patcher(meshPath, &builder.nif());

    // Both entries of the file match, the delete entry first.
    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*brick, matches));
    ASSERT_EQ(matches.size(), 1U);
    ASSERT_EQ(entriesOf(matches[0]).size(), 2U);
    EXPECT_EQ(entriesOf(matches[0])[0]["delete"], true);

    auto brickSlots = brickTextures;
    patcher.applyPatch(brickSlots, *brick, matches[0]);
    EXPECT_EQ(builder.nif().GetBlockID(brick), nifly::NIF_NPOS);
    ASSERT_EQ(builder.nif().GetShapes().size(), 1U);
    EXPECT_EQ(builder.nif().GetShapes()[0]->name.get(), "Keep");

    // The later entry reached neither the slots nor the shared shader through the deleted shape.
    EXPECT_EQ(brickSlots, brickTextures);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(sharedShader, nifly::SLSF2_UNUSED01));
    EXPECT_FLOAT_EQ(sharedShader->glossiness, glossiness);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       ApplyShaderAndGeometryAttributes)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->SetShaderType(nifly::BSLSP_PARALLAX);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_PARALLAX);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_FACEGEN_RGB_TINT);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    patcher.applyShader(*m_shape);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_PARALLAX));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_FACEGEN_RGB_TINT));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));

    // Geometry attributes: an entry with smoothing, automatic UVs and vertex color changes.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\geometry.json",
                        R"([{"texture": "a\\stone", "smooth_angle": 60, "auto_uv": 2, "vertex_colors": true,
                             "vertex_color_lum_mult": 0.5, "vertex_color_sat_mult": 0.5}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\geometry.json" }, false);
    m_shape->SetVertexColors(true);
    for (auto& vertex : m_shape->vertData)
        std::ranges::fill(vertex.colorData, static_cast<uint8_t>(100));
    const auto stone = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, stone);
    ASSERT_EQ(matches.size(), 1U);
    auto patched = stone;
    patcher.applyPatch(patched, *m_shape, matches[0]);
    EXPECT_GT(m_shader->uvScale.u, 1);
    EXPECT_FLOAT_EQ(m_shader->uvScale.u, m_shader->uvScale.v);
    // Half the distance to white: the gray gets brighter.
    EXPECT_GT(m_shape->vertData[0].colorData[0], 100);
    EXPECT_EQ(m_shape->vertData[0].colorData[0], m_shape->vertData[0].colorData[1]);

    const auto fromFactory = PatcherMeshShaderTruePBR::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "TruePBR");
}

TEST_F(PatcherMeshShaderTruePBRTest,
       AlphaPropertyEntriesChangeCreateAndDeleteTheAlphaProperty)
{
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\alpha.json", R"([
  {"texture": "a\\stone", "alphaprop_flags": 4845, "alphaprop_thresh": 64},
  {"texture": "c\\metal", "alphaprop_delete": true},
  {"texture": "c\\metal", "alphaprop_create": true},
  {"texture": "c\\cloth", "alphaprop_create": true, "alphaprop_flags": 4109, "alphaprop_thresh": 0},
  {"texture": "a\\cobble01", "alphaprop_delete": true, "alphaprop_create": true, "alphaprop_flags": 1},
  {"texture": "a\\exact", "alphaprop_flags": 65536, "alphaprop_thresh": 256, "alphaprop_create": "yes"},
  {"texture": "a\\fuzzy", "alphaprop_flags": -1, "alphaprop_thresh": 1.5, "alphaprop_create": true,
   "lock_rmaos": true}
])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\alpha.json" }, false);
    auto& nif = m_builder.nif();
    PatcherMeshShaderTruePBR patcher(meshPath, &nif);
    const auto apply = [&](nifly::NiShape& shape, const std::wstring& diffuse) {
        auto patched = slots(diffuse);
        const auto matches = matchesFor(patcher, patched);
        ASSERT_EQ(matches.size(), 1U) << TestUtil::pathToUTF8(diffuse);
        patcher.applyPatch(patched, shape, matches[0]);
    };

    // Flags and threshold only change an alpha property the shape already has.
    apply(*m_shape, L"textures\\a\\stone.dds");
    EXPECT_EQ(nif.GetAlphaProperty(m_shape), nullptr);
    const auto* const alpha = m_builder.addAlphaProperty(m_shape);
    apply(*m_shape, L"textures\\a\\stone.dds");
    EXPECT_EQ(alpha->flags, 4845U);
    EXPECT_EQ(alpha->threshold, 64U);

    // Create adds one with the defaults, or with the values of the entry. A delete entry removes the property first,
    // and the later entry of the same file adds the new one, so the old values are gone.
    auto* metal = m_builder.addShape("Metal", slots(L"textures\\c\\metal.dds"));
    m_builder.addAlphaProperty(metal, 4845, 64);
    const auto metalBlocks = nif.GetHeader().GetNumBlocks();
    apply(*metal, L"textures\\c\\metal.dds");
    ASSERT_NE(nif.GetAlphaProperty(metal), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(metal)->flags, 4844U);
    EXPECT_EQ(nif.GetAlphaProperty(metal)->threshold, 128U);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), metalBlocks);
    auto* cloth = m_builder.addShape("Cloth", slots(L"textures\\c\\cloth.dds"));
    apply(*cloth, L"textures\\c\\cloth.dds");
    ASSERT_NE(nif.GetAlphaProperty(cloth), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(cloth)->flags, 4109U);
    EXPECT_EQ(nif.GetAlphaProperty(cloth)->threshold, 0U);

    // Delete wins over the other three attributes of the same entry.
    auto* cobble = m_builder.addShape("Cobble", slots(L"textures\\a\\cobble01.dds"));
    m_builder.addAlphaProperty(cobble);
    const auto numBlocks = nif.GetHeader().GetNumBlocks();
    apply(*cobble, L"textures\\a\\cobble01.dds");
    EXPECT_EQ(nif.GetAlphaProperty(cobble), nullptr);
    EXPECT_EQ(nif.GetHeader().GetNumBlocks(), numBlocks - 1);

    // Values outside the range of the field and booleans of another type are ignored.
    auto* exact = m_builder.addShape("Exact", slots(L"textures\\a\\exact.dds"));
    const auto* const exactAlpha = m_builder.addAlphaProperty(exact);
    apply(*exact, L"textures\\a\\exact.dds");
    EXPECT_EQ(exactAlpha->flags, 4844U);
    EXPECT_EQ(exactAlpha->threshold, 128U);
    auto* exactNoAlpha = m_builder.addShape("ExactNoAlpha", slots(L"textures\\a\\exact.dds"));
    apply(*exactNoAlpha, L"textures\\a\\exact.dds");
    EXPECT_EQ(nif.GetAlphaProperty(exactNoAlpha), nullptr);
    auto* fuzzy = m_builder.addShape("Fuzzy", slots(L"textures\\a\\fuzzy.dds"));
    apply(*fuzzy, L"textures\\a\\fuzzy.dds");
    ASSERT_NE(nif.GetAlphaProperty(fuzzy), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(fuzzy)->flags, 4844U);
    EXPECT_EQ(nif.GetAlphaProperty(fuzzy)->threshold, 128U);

    // An alpha property two shapes share is copied for the shape the entry changes.
    auto* sharedA = m_builder.addShape("SharedA", slots(L"textures\\a\\stone.dds"));
    auto* sharedB = m_builder.addShape("SharedB", slots(L"textures\\a\\stone.dds"));
    auto* sharedAlpha = m_builder.addAlphaProperty(sharedA);
    sharedB->AlphaPropertyRef()->index = sharedA->AlphaPropertyRef()->index;
    apply(*sharedA, L"textures\\a\\stone.dds");
    EXPECT_NE(sharedA->AlphaPropertyRef()->index, sharedB->AlphaPropertyRef()->index);
    EXPECT_EQ(nif.GetAlphaProperty(sharedB), sharedAlpha);
    EXPECT_EQ(sharedAlpha->flags, 4844U);
    ASSERT_NE(nif.GetAlphaProperty(sharedA), nullptr);
    EXPECT_EQ(nif.GetAlphaProperty(sharedA)->flags, 4845U);
    EXPECT_EQ(nif.GetAlphaProperty(sharedA)->threshold, 64U);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       LoadFormatFollowsTheVersionOfTheFeatureINI)
{
    using PGEnums::TruePBRFormat;

    const auto formatFor = [this](const std::string& ini) {
        TestUtil::writeFile(dataDir() / L"Shaders\\Features\\TruePBR.ini", ini);
        PatcherMeshShaderTruePBR::setFormat(TruePBRFormat::Standalone);
        PatcherMeshShaderTruePBR::loadFormat(dataDir());
        const auto format = PatcherMeshShaderTruePBR::format();
        return format;
    };

    // Without the feature INI the current format is written.
    PatcherMeshShaderTruePBR::setFormat(TruePBRFormat::Legacy);
    PatcherMeshShaderTruePBR::loadFormat(dataDir());
    EXPECT_EQ(PatcherMeshShaderTruePBR::format(), TruePBRFormat::Standalone);
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, "Unable to read the version"));

    EXPECT_EQ(formatFor("[Info]\nVersion = 1-3-0\n"), TruePBRFormat::Legacy);
    EXPECT_EQ(formatFor("[Info]\nVersion = 0-9-0\n"), TruePBRFormat::Legacy);
    EXPECT_EQ(formatFor("[Info]\nVersion = 1-99-99\n"), TruePBRFormat::Legacy);
    EXPECT_EQ(formatFor("[Info]\r\nVersion=1-3-0\r\n"), TruePBRFormat::Legacy);
    EXPECT_EQ(formatFor("[info]\nversion = 1.3.0\n"), TruePBRFormat::Legacy);
    EXPECT_EQ(formatFor("[Info]\nVersion = 2-0-0\n"), TruePBRFormat::Standalone);
    EXPECT_EQ(formatFor("[Info]\nVersion = 2-1-3\n"), TruePBRFormat::Standalone);
    EXPECT_EQ(formatFor("[Info]\nVersion = 10-0-0\n"), TruePBRFormat::Standalone);
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, "Unable to read the version"));

    // A file that does not say which version it is counts as a missing one, with a warning.
    EXPECT_EQ(formatFor("[Info]\nVersion = unknown\n"), TruePBRFormat::Standalone);
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "Unable to read the version"));
    EXPECT_EQ(formatFor("[Other]\nVersion = 1-3-0\n"), TruePBRFormat::Standalone);
    EXPECT_EQ(formatFor(""), TruePBRFormat::Standalone);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       TheLegacyFormatReplacesAStandaloneShader)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->SetShaderType(nifly::BSLSP_TRUEPBR);

    auto metal = slots(L"textures\\c\\metal.dds");
    auto matches = matchesFor(patcher, metal);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(metal, *m_shape, matches[0]);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_FLOAT_EQ(m_shader->glossiness, 0.2F);
}

/**
 * @brief The same data folder, with shapes written for Community Shaders 2.0 and later.
 */
class PatcherMeshShaderTruePBRStandaloneTest : public PatcherMeshShaderTruePBRTest {
protected:
    void SetUp() override
    {
        PatcherMeshShaderTruePBRTest::SetUp();
        PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Standalone);
    }

    /// @brief Replaces the configs with one that has the given entries.
    void loadConfig(const std::string& entries)
    {
        TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\standalone.json", entries);
        m_pgd->populateFileMap(false, false);
        PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\standalone.json" }, false);
    }

    /// @brief Gives the shape the legacy PBR flag with an existing RMAOS, the way a mod ships it pre-patched.
    PGTypes::TextureSet markLegacyPrePatched()
    {
        auto prePatched = slots(L"textures\\a\\rmaos.dds", L"textures\\a\\stone_n.dds");
        prePatched[4] = L"textures\\cubemaps\\c_e.dds";
        prePatched[5] = L"textures\\a\\rmaos_rmaos.dds";
        m_builder.setTextures(m_shape, prePatched);
        PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_UNUSED01);
        return prePatched;
    }

    /// @brief Runs the legacy PBR pre patcher on the shape.
    bool convertLegacy(PGTypes::TextureSet& slotsToConvert)
    {
        PatcherMeshPreConvertLegacyPBR patcher(meshPath, &m_builder.nif());
        return patcher.applyPatch(slotsToConvert, *m_shape);
    }

    /// @brief Applies the pre-patched match of a shape.
    void applyPrePatchedMatch(PGTypes::TextureSet& slotsToPatch)
    {
        PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
        std::vector<PatcherMatch> matches;
        ASSERT_TRUE(patcher.shouldApply(*m_shape, matches));
        ASSERT_FALSE(matches[0].extraData);
        patcher.applyPatch(slotsToPatch, *m_shape, matches[0]);
    }
};

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       ApplyPatchWritesThePayloadInsteadOfTheLegacyFields)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->SetShaderType(nifly::BSLSP_ENVMAP);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_GLOW_MAP);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING);
    const auto glossiness = m_shader->glossiness;
    const auto specularStrength = m_shader->specularStrength;
    const auto softlighting = m_shader->softlighting;
    const auto rimlightPower = m_shader->rimlightPower;

    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    patched[4] = L"textures\\cubemaps\\c_e.dds";
    patched[8] = L"textures\\a\\unused.dds";
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 2U);
    patcher.applyPatch(patched, *m_shape, matches[1]); // the a.json entry

    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    const auto& pbr = m_shader->truePBR;
    EXPECT_EQ(pbr.version, 2U);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_SUBSURFACE);
    EXPECT_FLOAT_EQ(pbr.specularLevel, 0.04F);
    EXPECT_FLOAT_EQ(pbr.roughnessScale, 1.5F);
    EXPECT_EQ(pbr.subsurfaceColor, nifly::Vector3(1, 0.5F, 0.25F));
    EXPECT_FLOAT_EQ(pbr.subsurfaceOpacity, 0.8F);
    EXPECT_FLOAT_EQ(pbr.displacementScale, 0.3F);

    // The fields of the legacy format stay what they were.
    EXPECT_FLOAT_EQ(m_shader->glossiness, glossiness);
    EXPECT_FLOAT_EQ(m_shader->specularStrength, specularStrength);
    EXPECT_FLOAT_EQ(m_shader->softlighting, softlighting);
    EXPECT_FLOAT_EQ(m_shader->rimlightPower, rimlightPower);

    // The fields both formats share work as before.
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_EXTERNAL_EMITTANCE));
    EXPECT_FLOAT_EQ(m_shader->emissiveMultiple, 3);
    EXPECT_EQ(m_shader->emissiveColor, nifly::Vector3(1, 0.5F, 0.25F));
    EXPECT_FLOAT_EQ(m_shader->uvScale.u, 2);

    // No flag carries the material, and none that Community Shaders rejects is left.
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_GLOW_MAP));

    EXPECT_EQ(patched[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(patched[1], L"textures\\pbr\\a\\stone_n.dds");
    EXPECT_EQ(patched[2], L"textures\\pbr\\a\\stone_g.dds");
    EXPECT_EQ(patched[3], L"textures\\pbr\\a\\stone_p.dds");
    EXPECT_TRUE(patched[4].empty());
    EXPECT_EQ(patched[5], L"textures\\pbr\\a\\stone_rmaos.dds");
    EXPECT_EQ(patched[7], L"textures\\pbr\\a\\stone_s.dds");
    EXPECT_TRUE(patched[8].empty());

    // A later entry changes what it names and leaves the rest of the payload alone.
    patcher.applyPatch(patched, *m_shape, matches[0]);
    EXPECT_FLOAT_EQ(pbr.specularLevel, 0.5F);
    EXPECT_FLOAT_EQ(pbr.roughnessScale, 1.5F);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_SUBSURFACE);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       ShapesThatWereNotPBRStartFromTheNeutralPayload)
{
    loadConfig(R"([{"texture": "a\\stone"}])");
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->glossiness = 80;
    m_shader->specularColor = nifly::Vector3(0.5F, 0.5F, 0.5F);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_EFFECT_LIGHTING);

    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(patched, *m_shape, matches[0]);

    const nifly::TruePBRData neutral;
    EXPECT_EQ(m_shader->truePBR.features, 0U);
    EXPECT_FLOAT_EQ(m_shader->truePBR.specularLevel, neutral.specularLevel);
    EXPECT_EQ(m_shader->truePBR.subsurfaceColor, neutral.subsurfaceColor);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       LayerEntriesSetTheirFeaturesAndValues)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    const auto& pbr = m_shader->truePBR;

    // Interlayer parallax needs a height map, which this entry does not have.
    auto cobble = slots(L"textures\\a\\cobble01.dds");
    auto matches = matchesFor(patcher, cobble);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(cobble, *m_shape, matches[0]);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_TWO_LAYER | nifly::TRUEPBR_COLORED_COAT | nifly::TRUEPBR_COAT_NORMAL);
    EXPECT_EQ(pbr.coatColor, nifly::Vector3(0.1F, 0.2F, 0.3F));
    EXPECT_FLOAT_EQ(pbr.coatSpecularLevel, 0.5F);
    EXPECT_FLOAT_EQ(pbr.coatRoughness, 0.6F);
    EXPECT_FLOAT_EQ(pbr.coatStrength, 0.7F);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_EQ(cobble[6], L"textures\\pbr\\a\\cobble01_cnr.dds");
    EXPECT_EQ(cobble[7], L"textures\\pbr\\a\\cobble01_s.dds");

    // Every entry decides the layers, so the coat goes.
    auto exact = slots(L"textures\\a\\exact.dds");
    matches = matchesFor(patcher, exact);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(exact, *m_shape, matches[0]);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_GLINT);
    EXPECT_FLOAT_EQ(pbr.glintScreenSpaceScale, 1.5F);
    EXPECT_FLOAT_EQ(pbr.glintLogMicrofacetDensity, 2.5F);
    EXPECT_FLOAT_EQ(pbr.glintMicrofacetRoughness, 0.3F);
    EXPECT_FLOAT_EQ(pbr.glintDensityRandomization, 0.4F);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE));

    // A plain entry leaves a plain PBR material, still with the shader type of the format.
    auto metal = slots(L"textures\\c\\metal.dds");
    matches = matchesFor(patcher, metal);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(metal, *m_shape, matches[0]);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    EXPECT_EQ(pbr.features, 0U);
    EXPECT_FLOAT_EQ(pbr.specularLevel, 0.2F);

    // Hair cannot be combined with anything, as fuzz and hair never could.
    auto fuzzy = slots(L"textures\\a\\fuzzy.dds", L"textures\\a\\fuzzy_n.dds");
    fuzzy[5] = L"textures\\a\\keep_rmaos.dds";
    matches = matchesFor(patcher, fuzzy);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(fuzzy, *m_shape, matches[0]);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_HAIR_MARSCHNER);
    EXPECT_EQ(pbr.fuzzColor, nifly::Vector3(0.1F, 0.2F, 0.3F));
    EXPECT_FLOAT_EQ(pbr.fuzzWeight, 0.9F);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_BACK_LIGHTING));
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       AttributesThatExcludeEachOtherFollowTheLegacyOrder)
{
    const auto featuresOf = [this](const std::string& entry) {
        loadConfig(entry);
        auto* shape = m_builder.addShape("Shape", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
        auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
        auto matches = matchesFor(patcher, patched);
        EXPECT_EQ(matches.size(), 1U);
        patcher.applyPatch(patched, *shape, matches.at(0));
        return m_builder.lightingShader(shape)->truePBR.features;
    };

    // The coat beats everything else, which keeps the material valid for Community Shaders.
    EXPECT_EQ(featuresOf(R"([{"texture": "a\\stone", "multilayer": true, "subsurface": true, "hair": true,
                              "glint": {}, "fuzz": {}}])"),
              nifly::TRUEPBR_TWO_LAYER);
    EXPECT_EQ(featuresOf(R"([{"texture": "a\\stone", "subsurface": true, "hair": true}])"),
              nifly::TRUEPBR_HAIR_MARSCHNER);
    EXPECT_EQ(featuresOf(R"([{"texture": "a\\stone", "glint": {}, "fuzz": {}}])"), nifly::TRUEPBR_GLINT);
    EXPECT_EQ(featuresOf(R"([{"texture": "a\\stone", "subsurface": true, "fuzz": {}}])"),
              nifly::TRUEPBR_SUBSURFACE | nifly::TRUEPBR_FUZZ);

    // The coat options only exist with a coat.
    EXPECT_EQ(featuresOf(R"([{"texture": "a\\stone", "coat_diffuse": true, "coat_parallax": true}])"), 0U);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       TheSubsurfaceAttributesStillWorkForACoat)
{
    loadConfig(R"([{"texture": "a\\stone", "multilayer": true, "subsurface_color": [0.1, 0.2, 0.3],
                    "subsurface_opacity": 0.4}])");
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(patched, *m_shape, matches[0]);

    EXPECT_EQ(m_shader->truePBR.coatColor, nifly::Vector3(0.1F, 0.2F, 0.3F));
    EXPECT_FLOAT_EQ(m_shader->truePBR.coatStrength, 0.4F);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       ValuesStayWithinWhatCommunityShadersAccepts)
{
    loadConfig(R"([{"texture": "a\\stone", "specular_level": 5, "roughness_scale": -1, "subsurface_opacity": 2,
                    "subsurface_color": [-1, 0.5, 2], "displacement_scale": -3, "emissive_scale": -2,
                    "glint": {"screen_space_scale": 0.5, "log_microfacet_density": 50, "microfacet_roughness": 0.9,
                              "density_randomization": 9}}])");
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_shader->alpha = 500;
    m_shader->refractionStrength = 3;
    m_shader->textureClampMode = static_cast<nifly::TexClampMode>(9);

    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(patched, *m_shape, matches[0]);

    const auto& pbr = m_shader->truePBR;
    EXPECT_FLOAT_EQ(pbr.specularLevel, 1);
    EXPECT_FLOAT_EQ(pbr.roughnessScale, 0);
    EXPECT_FLOAT_EQ(pbr.subsurfaceOpacity, 1);
    EXPECT_EQ(pbr.subsurfaceColor, nifly::Vector3(0, 0.5F, 2));
    EXPECT_FLOAT_EQ(pbr.displacementScale, 0);
    EXPECT_FLOAT_EQ(pbr.glintScreenSpaceScale, 1);
    EXPECT_FLOAT_EQ(pbr.glintLogMicrofacetDensity, 40);
    EXPECT_FLOAT_EQ(pbr.glintMicrofacetRoughness, 0.3F);
    EXPECT_FLOAT_EQ(pbr.glintDensityRandomization, 5);
    EXPECT_FLOAT_EQ(m_shader->emissiveMultiple, 0);
    EXPECT_FLOAT_EQ(m_shader->alpha, 128);
    EXPECT_FLOAT_EQ(m_shader->refractionStrength, 1);
    EXPECT_EQ(m_shader->textureClampMode, nifly::WRAP_S_WRAP_T);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       CubemapAndSlotAttributesCannotFillTheReservedSlots)
{
    loadConfig(R"([{"texture": "a\\stone", "cubemap": "textures\\cubemaps\\c_e.dds",
                    "slot5": "cubemaps\\c_e.dds"}])");
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(patched, *m_shape, matches[0]);
    EXPECT_TRUE(patched[4].empty());
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       MatchesWithoutTheRequiredTexturesAreDropped)
{
    loadConfig(R"([{"texture": "a\\stone", "lock_rmaos": true}])");
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());

    // Community Shaders would not draw the shape, so it stays what it is even with the path checks off.
    PatcherMeshShaderTruePBR::loadOptions(false, false);
    const auto noRMAOS = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    EXPECT_TRUE(matchesFor(patcher, noRMAOS).empty());

    // The legacy format has no such rule.
    PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Legacy);
    EXPECT_EQ(matchesFor(patcher, noRMAOS).size(), 1U);
    PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Standalone);

    auto withRMAOS = noRMAOS;
    withRMAOS[5] = L"textures\\a\\keep_rmaos.dds";
    EXPECT_EQ(matchesFor(patcher, withRMAOS).size(), 1U);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       TheSlotsOfAnAlternateTextureReachTheMesh)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    const auto vanilla = PGNIFUtil::textureSlots(&m_builder.nif(), m_shape);
    EXPECT_TRUE(vanilla[5].empty());

    // Slots of an alternate texture are not the ones of the mesh, which has no RMAOS and would be rejected as it is.
    auto alternate = slots(L"textures\\c\\metal.dds", L"textures\\c\\metal_n.dds");
    auto matches = matchesFor(patcher, alternate);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(alternate, *m_shape, matches[0]);

    EXPECT_EQ(alternate[5], L"textures\\pbr\\c\\metal_rmaos.dds");
    EXPECT_EQ(PGNIFUtil::textureSlots(&m_builder.nif(), m_shape), alternate);

    // The legacy format only updates the alternate texture.
    NIFBuilder builder;
    auto* shape = builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Legacy);
    PatcherMeshShaderTruePBR legacy(meshPath, &builder.nif());
    auto legacyAlternate = slots(L"textures\\c\\metal.dds", L"textures\\c\\metal_n.dds");
    matches = matchesFor(legacy, legacyAlternate);
    ASSERT_EQ(matches.size(), 1U);
    legacy.applyPatch(legacyAlternate, *shape, matches[0]);
    EXPECT_EQ(PGNIFUtil::textureSlots(&builder.nif(), shape), vanilla);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       PatchedShapesSurviveSavingAndLoading)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    m_builder.addShape("Next", slots(L"textures\\a\\wood.dds"));
    auto patched = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    auto matches = matchesFor(patcher, patched);
    ASSERT_EQ(matches.size(), 2U);
    patcher.applyPatch(patched, *m_shape, matches[1]);

    auto loaded = PGNIFUtil::loadNIFFromBytes(m_builder.bytes(), false);
    ASSERT_EQ(loaded.GetShapes().size(), 2U);
    auto* const shader = dynamic_cast<nifly::BSLightingShaderProperty*>(loaded.GetShader(loaded.GetShapes()[0]));
    ASSERT_NE(shader, nullptr);
    EXPECT_EQ(shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    EXPECT_EQ(shader->truePBR.features, nifly::TRUEPBR_SUBSURFACE);
    EXPECT_FLOAT_EQ(shader->truePBR.roughnessScale, 1.5F);
    EXPECT_EQ(loaded.GetHeader().GetBlockSize(loaded.GetBlockID(shader)), 180U);
    EXPECT_EQ(PGNIFUtil::textureSlots(&loaded, loaded.GetShapes()[0])[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(PGNIFUtil::textureSlots(&loaded, loaded.GetShapes()[1])[0], L"textures\\a\\wood.dds");
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       PrePatchedLegacyShapesAreConvertedWithTheirMaterial)
{
    auto prePatched = markLegacyPrePatched();
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_ANISOTROPIC_LIGHTING);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING);
    m_shader->SetShaderType(nifly::BSLSP_MULTILAYERPARALLAX);
    m_shader->glossiness = 0.05F;
    m_shader->specularStrength = 1.25F;
    m_shader->rimlightPower = 0.5F;
    m_shader->specularColor = nifly::Vector3(0.25F, 0.5F, 0.75F);
    m_shader->softlighting = 0.6F;
    m_shader->parallaxInnerLayerThickness = 0.1F;
    m_shader->parallaxRefractionScale = 0.2F;
    m_shader->parallaxInnerLayerTextureScale = nifly::Vector2(0.3F, 0.4F);

    EXPECT_TRUE(convertLegacy(prePatched));

    const auto& pbr = m_shader->truePBR;
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_SUBSURFACE | nifly::TRUEPBR_FUZZ | nifly::TRUEPBR_THIN_SURFACE);
    EXPECT_FLOAT_EQ(pbr.specularLevel, 0.05F);
    EXPECT_FLOAT_EQ(pbr.roughnessScale, 1.25F);
    EXPECT_FLOAT_EQ(pbr.displacementScale, 0.5F);
    EXPECT_EQ(pbr.subsurfaceColor, nifly::Vector3(0.25F, 0.5F, 0.75F));
    EXPECT_FLOAT_EQ(pbr.subsurfaceOpacity, 0.6F);
    EXPECT_EQ(pbr.fuzzColor, nifly::Vector3(0.1F, 0.2F, 0.3F));
    EXPECT_FLOAT_EQ(pbr.fuzzWeight, 0.4F);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_RIM_LIGHTING));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_SOFT_LIGHTING));
    EXPECT_TRUE(prePatched[4].empty());
    EXPECT_EQ(PGNIFUtil::textureSlots(&m_builder.nif(), m_shape), prePatched);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       PrePatchedLegacyCoatsAndGlintsKeepTheirLayer)
{
    auto prePatched = markLegacyPrePatched();
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_EFFECT_LIGHTING);
    m_shader->SetShaderType(nifly::BSLSP_MULTILAYERPARALLAX);
    m_shader->specularColor = nifly::Vector3(0.25F, 0.5F, 0.75F);
    m_shader->softlighting = 0.6F;
    m_shader->parallaxInnerLayerThickness = 0.1F;
    m_shader->parallaxRefractionScale = 0.2F;

    EXPECT_TRUE(convertLegacy(prePatched));

    const auto& coat = m_shader->truePBR;
    EXPECT_EQ(coat.features, nifly::TRUEPBR_TWO_LAYER | nifly::TRUEPBR_COLORED_COAT);
    EXPECT_EQ(coat.coatColor, nifly::Vector3(0.25F, 0.5F, 0.75F));
    EXPECT_FLOAT_EQ(coat.coatStrength, 0.6F);
    EXPECT_FLOAT_EQ(coat.coatRoughness, 0.1F);
    EXPECT_FLOAT_EQ(coat.coatSpecularLevel, 0.2F);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));

    auto* glint = m_builder.addShape("Glint", prePatched);
    m_shape = glint;
    m_shader = m_builder.lightingShader(glint);
    prePatched = markLegacyPrePatched();
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE);
    m_shader->SetShaderType(nifly::BSLSP_MULTILAYERPARALLAX);
    m_shader->parallaxInnerLayerThickness = 1.5F;
    m_shader->parallaxRefractionScale = 20;
    m_shader->parallaxInnerLayerTextureScale = nifly::Vector2(0.02F, 3);

    EXPECT_TRUE(convertLegacy(prePatched));

    const auto& pbr = m_shader->truePBR;
    EXPECT_EQ(pbr.features, nifly::TRUEPBR_GLINT);
    EXPECT_FLOAT_EQ(pbr.glintScreenSpaceScale, 1.5F);
    EXPECT_FLOAT_EQ(pbr.glintLogMicrofacetDensity, 20);
    EXPECT_FLOAT_EQ(pbr.glintMicrofacetRoughness, 0.02F);
    EXPECT_FLOAT_EQ(pbr.glintDensityRandomization, 3);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE));
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       PrePatchedStandaloneShapesStayAsTheyAre)
{
    auto prePatched = slots(L"textures\\a\\rmaos.dds", L"textures\\a\\stone_n.dds");
    prePatched[5] = L"textures\\a\\rmaos_rmaos.dds";
    m_builder.setTextures(m_shape, prePatched);
    m_shader->SetShaderType(nifly::BSLSP_TRUEPBR);
    m_shader->truePBR.features = nifly::TRUEPBR_FOLIAGE;
    m_shader->truePBR.specularLevel = 0.5F;
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE);
    const auto textures = prePatched;

    EXPECT_FALSE(convertLegacy(prePatched));
    applyPrePatchedMatch(prePatched);

    EXPECT_EQ(m_shader->truePBR.features, nifly::TRUEPBR_FOLIAGE);
    EXPECT_FLOAT_EQ(m_shader->truePBR.specularLevel, 0.5F);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_FIT_SLOPE));
    EXPECT_EQ(prePatched, textures);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       PrePatchedLegacyShapesStayLegacyForOlderCommunityShaders)
{
    auto prePatched = markLegacyPrePatched();
    const auto textures = prePatched;
    PatcherMeshShaderTruePBR::setFormat(PGEnums::TruePBRFormat::Legacy);

    EXPECT_FALSE(convertLegacy(prePatched));
    applyPrePatchedMatch(prePatched);

    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_EQ(prePatched, textures);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       OnlyThePrePatcherConvertsLegacyShapes)
{
    auto prePatched = markLegacyPrePatched();
    const auto textures = prePatched;

    applyPrePatchedMatch(prePatched);

    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_EQ(prePatched, textures);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       LegacyShapesWithoutARequiredTextureAreNotConverted)
{
    const auto prePatched = markLegacyPrePatched();
    for (const size_t slot : { 0U, 1U, 5U }) {
        auto missingTexture = prePatched;
        missingTexture.at(slot).clear();
        EXPECT_FALSE(convertLegacy(missingTexture));
        EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
        EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    }
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       ShapesThatAreNotLegacyPBROrHavePGIgnoreAreNotConverted)
{
    // No flag, so a vanilla shape, even with an RMAOS texture.
    auto vanilla = slots(L"textures\\a\\rmaos.dds", L"textures\\a\\stone_n.dds");
    vanilla[5] = L"textures\\a\\rmaos_rmaos.dds";
    m_builder.setTextures(m_shape, vanilla);
    EXPECT_FALSE(convertLegacy(vanilla));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);

    auto ignored = markLegacyPrePatched();
    m_builder.addBooleanExtraData(m_shape, "PG_IGNORE", true);
    const auto textures = ignored;
    EXPECT_FALSE(convertLegacy(ignored));
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_EQ(ignored, textures);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       EntriesBuildOnTheMaterialOfAConvertedShape)
{
    loadConfig(R"([{"texture": "a\\stone", "specular_level": 0.5}])");
    auto legacy = markLegacyPrePatched();
    m_shader->specularStrength = 1.25F;
    m_shader->glossiness = 0.1F;
    legacy[0] = L"textures\\a\\stone.dds";
    EXPECT_TRUE(convertLegacy(legacy));
    EXPECT_FLOAT_EQ(m_shader->truePBR.specularLevel, 0.1F);

    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    auto matches = matchesFor(patcher, legacy);
    ASSERT_EQ(matches.size(), 1U);
    patcher.applyPatch(legacy, *m_shape, matches[0]);

    EXPECT_FLOAT_EQ(m_shader->truePBR.specularLevel, 0.5F);
    EXPECT_FLOAT_EQ(m_shader->truePBR.roughnessScale, 1.25F);
}

TEST_F(PatcherMeshShaderTruePBRStandaloneTest,
       ApplyShaderSetsThePlainShader)
{
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_PARALLAX);
    patcher.applyShader(*m_shape);

    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_TRUEPBR);
    EXPECT_EQ(m_shader->truePBR.features, 0U);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_UNUSED01));
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_PARALLAX));
}
}
