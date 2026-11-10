#include "patchers/PatcherMeshShaderTruePBR.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
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
#include <limits>
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
    }

    void TearDown() override
    {
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
       EntriesWithAKeyOfTheWrongTypeAreSkippedWithAWarning)
{
    // Every entry but the last is not an object or has a key that selects shapes or textures with a value of another
    // type. Each of them used to throw, while loading or while matching the first shape.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\types.json", R"([
  {"match_diffuse": 5},
  {"texture": ["a\\stone"]},
  {"match_normal": true, "specular_level": 0.3},
  {"path_contains": 5},
  {"match1": {"path": "a\\exact.dds"}},
  {"texture": "a\\stone", "nif_filter": 5},
  {"texture": "a\\stone", "rename": null},
  5,
  "a\\stone",
  {"texture": "a\\stone", "specular_level": 0.5}
])");
    // The defaults are merged into an entry before it is checked, and the values of an object holding the entries are
    // the entries.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\defaults.json",
                        R"({"default": {"path_contains": 5}, "entries": [{"texture": "c\\metal"}]})");
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\entries.json",
                        R"({"default": {}, "entries": {"texture": "c\\cloth"}})");
    m_pgd->populateFileMap(false, false);
    ASSERT_NO_THROW(PatcherMeshShaderTruePBR::loadStatics(
        { L"pbrnifpatcher\\types.json", L"pbrnifpatcher\\defaults.json", L"pbrnifpatcher\\entries.json" }, false));

    const auto& configs = PatcherMeshShaderTruePBR::truePBRConfigs();
    ASSERT_EQ(configs.size(), 1U);
    EXPECT_EQ(configs.at(0)["specular_level"], 0.5);
    EXPECT_EQ(PatcherMeshShaderTruePBR::truePBRDiffuseInverse().size(), 1U);
    EXPECT_TRUE(PatcherMeshShaderTruePBR::truePBRNormalInverse().empty());
    EXPECT_TRUE(PatcherMeshShaderTruePBR::pathLookupJSONs().empty());
    EXPECT_TRUE(PatcherMeshShaderTruePBR::truePBRMatchXMap().empty());

    // The warning names the file, the position of the entry in it and the key.
    for (const auto* message : {
             R"(Skipping entry 1 of PBR json "pbrnifpatcher\types.json": "match_diffuse" is not a string (number))",
             R"(Skipping entry 2 of PBR json "pbrnifpatcher\types.json": "texture" is not a string (array))",
             R"(Skipping entry 3 of PBR json "pbrnifpatcher\types.json": "match_normal" is not a string (boolean))",
             R"(Skipping entry 4 of PBR json "pbrnifpatcher\types.json": "path_contains" is not a string (number))",
             R"(Skipping entry 5 of PBR json "pbrnifpatcher\types.json": "match1" is not a string (object))",
             R"(Skipping entry 6 of PBR json "pbrnifpatcher\types.json": "nif_filter" is not a string (number))",
             R"(Skipping entry 7 of PBR json "pbrnifpatcher\types.json": "rename" is not a string (null))",
             R"(Skipping entry 8 of PBR json "pbrnifpatcher\types.json": the entry is not an object (number))",
             R"(Skipping entry 9 of PBR json "pbrnifpatcher\types.json": the entry is not an object (string))",
             R"(Skipping entry 1 of PBR json "pbrnifpatcher\defaults.json": "path_contains" is not a string (number))",
             R"(Skipping entry 1 of PBR json "pbrnifpatcher\entries.json": the entry is not an object (string))",
         }) {
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, message)) << message;
    }
    EXPECT_EQ(TestLog::count(spdlog::level::warn, "Skipping entry"), 11U);

    // Only the valid entry matches, and matching no longer throws.
    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    const auto matches = matchesFor(patcher, slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    ASSERT_EQ(matches.size(), 1U);
    ASSERT_EQ(entriesOf(matches[0]).size(), 1U);
    EXPECT_EQ(entriesOf(matches[0])[0]["specular_level"], 0.5);
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\a\\exact.dds")).empty());
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\c\\metal.dds")).empty());
    EXPECT_TRUE(matchesFor(patcher, slots(L"textures\\c\\cloth.dds")).empty());
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
       ACoatColorThatIsNotAnArrayIsIgnored)
{
    // An object with three members passes the size check of the coat color.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\coat.json",
                        R"([{"texture": "a\\stone", "multilayer": true, "coat_color": {"r": 1, "g": 0, "b": 0},
                             "coat_strength": 0.7}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\coat.json" }, false);

    PatcherMeshShaderTruePBR patcher(meshPath, &m_builder.nif());
    const auto stone = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    const auto matches = matchesFor(patcher, stone);
    ASSERT_EQ(matches.size(), 1U);
    const auto specularColor = m_shader->specularColor;
    auto patched = stone;
    EXPECT_NO_THROW(patcher.applyPatch(patched, *m_shape, matches[0]));

    // The rest of the entry still applies.
    EXPECT_EQ(patched[0], L"textures\\pbr\\a\\stone.dds");
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_MULTILAYERPARALLAX);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF2_MULTI_LAYER_PARALLAX));
    EXPECT_EQ(m_shader->specularColor, specularColor);
    EXPECT_FLOAT_EQ(m_shader->softlighting, 0.7F); // coat strength
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
       ADeletedShapeLeavesTheAlphaPropertyItSharesWithAnIgnoredShape)
{
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\delete.json", R"([{"texture": "b\\brick", "delete": true}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\delete.json" }, false);

    NIFBuilder builder;
    const auto brickTextures = slots(L"textures\\b\\brick.dds", L"textures\\b\\brick_n.dds");
    auto* ignored = builder.addShape("Ignored", brickTextures);
    builder.addBooleanExtraData(ignored, "PG_IGNORE", true);
    const auto* const alpha = builder.addAlphaProperty(ignored);
    auto* brick = builder.addShape("Brick", brickTextures);
    brick->AlphaPropertyRef()->index = ignored->AlphaPropertyRef()->index;
    PatcherMeshShaderTruePBR patcher(meshPath, &builder.nif());

    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*brick, matches));
    ASSERT_EQ(matches.size(), 1U);
    auto brickSlots = brickTextures;
    patcher.applyPatch(brickSlots, *brick, matches[0]);

    EXPECT_EQ(builder.nif().GetBlockID(brick), nifly::NIF_NPOS);
    EXPECT_EQ(builder.nif().GetAlphaProperty(ignored), alpha);
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
       MaterialCarriesTheAttributesOfTheAppliedEntries)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderTruePBR patcher(meshPath, &nif);
    // Applies the entries of the last match of a diffuse to the stone shape and describes the shape with what they
    // define, as a collector of its own collects it.
    const auto applyAndDescribe = [&](const std::wstring& diffuse) {
        const PatcherMeshShaderTruePBR::AppliedAttributes applied;
        auto patched = slots(diffuse);
        const auto matches = matchesFor(patcher, patched);
        EXPECT_FALSE(matches.empty()) << TestUtil::pathToUTF8(diffuse);
        if (!matches.empty())
            patcher.applyPatch(patched, *m_shape, matches.back());
        const auto* const attributes = applied.of(*m_shape);
        EXPECT_NE(attributes, nullptr) << TestUtil::pathToUTF8(diffuse);
        return attributes ? PatcherMeshShaderTruePBR::material(nif, *m_shape, patched, *attributes) : nlohmann::json();
    };
    const auto none = nlohmann::json::object();

    // A shape that is not PBR has no material, nor has a shape without a lighting shader.
    const auto stone = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    EXPECT_TRUE(PatcherMeshShaderTruePBR::material(nif, *m_shape, stone, none).is_null());
    auto* const effect = m_builder.addEffectShape("Glow", "textures\\fx\\glow.dds");
    EXPECT_TRUE(PatcherMeshShaderTruePBR::material(nif, *effect, PGTypes::TextureSet { }, none).is_null());

    // The material attributes of the a.json entry (the last match) as the config gives them, its UV scale among them,
    // and the textures relative to the textures folder. The entry's matching attribute, the file it came from and its
    // smooth_angle, which is not a number, are left out.
    auto material = applyAndDescribe(L"textures\\a\\stone.dds");
    EXPECT_EQ(material, nlohmann::json::parse(R"({
        "shader_type": "pbr", "emissive": true, "parallax": true, "subsurface": true, "specular_level": 0.04,
        "roughness_scale": 1.5, "subsurface_color": [1, 0.5, 0.25], "subsurface_opacity": 0.8,
        "displacement_scale": 0.3, "zbuffer_write": false, "vertex_colors": true, "uv_scale": 2, "emissive_scale": 3,
        "emissive_color": [1, 0.5, 0.25, 1], "slot1": "pbr\\a\\stone.dds", "slot2": "pbr\\a\\stone_n.dds",
        "slot3": "pbr\\a\\stone_g.dds", "slot4": "pbr\\a\\stone_p.dds", "slot6": "pbr\\a\\stone_rmaos.dds",
        "slot8": "pbr\\a\\stone_s.dds"})"));
    EXPECT_EQ(material["specular_level"].dump(), "0.04");

    // For entries without material attributes, the shader type and the textures are all there is. The textures are
    // the ones given, which is how an alternate texture of a record is described.
    const auto alternate = slots(L"textures\\alt\\stone.dds", L"alt\\stone_n.dds");
    const auto bare = nlohmann::json::parse(R"({
        "shader_type": "pbr", "slot1": "alt\\stone.dds", "slot2": "alt\\stone_n.dds"})");
    EXPECT_EQ(PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, none), bare);

    // The attributes that pick the shapes, the legacy ones, delete and the slot attributes (the slots are the
    // shape's) are no material attributes; values of a type the patch does not apply are left out too.
    EXPECT_EQ(PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, nlohmann::json::parse(R"({
                  "texture": "a\\stone", "match_diffuse": "a\\stone", "match1": "a\\stone.dds", "path_contains": "st",
                  "nif_filter": "clutter", "rename": "b", "json": "pbrnifpatcher\\a.json", "pbr": true,
                  "env_mapping": true, "delete": false, "slot1": "other.dds", "alphaprop_thresh": "low",
                  "emissive_color": 1, "glint": true, "cubemap": 5, "auto_uv": "yes"})")),
              bare);

    // The alpha property attributes as the entries give them, a field value as the integer the patch writes and only
    // when it fits the field.
    const auto alpha = nlohmann::json::parse(R"({"alphaprop_flags": 4845.0, "alphaprop_create": true,
                                                "alphaprop_delete": false, "alphaprop_thresh": 64})");
    material = PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, alpha);
    for (const auto& [key, value] : alpha.items())
        EXPECT_EQ(material[key], value) << key;
    EXPECT_EQ(material["alphaprop_flags"].dump(), "4845");
    material = PatcherMeshShaderTruePBR::material(
        nif, *m_shape, alternate, nlohmann::json { { "alphaprop_flags", 65536 }, { "alphaprop_thresh", 1.5 } });
    EXPECT_EQ(material, bare);

    // uv_scale is one number for both directions, as the entries give it. auto_uv computed the scale the shape has,
    // the same in both directions, which is written as uv_scale; a scale that is not finite is left out.
    m_shader->uvScale = nifly::Vector2(1.5, 1.5);
    material = PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, nlohmann::json { { "auto_uv", 2 } });
    EXPECT_EQ(material["uv_scale"], 1.5);
    EXPECT_FALSE(material.contains("auto_uv"));
    material = PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, nlohmann::json { { "uv_scale", 0.3 } });
    EXPECT_EQ(material["uv_scale"].dump(), "0.3");
    m_shader->uvScale = nifly::Vector2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
    EXPECT_EQ(PatcherMeshShaderTruePBR::material(nif, *m_shape, alternate, nlohmann::json { { "auto_uv", 2 } }), bare);

    // Two layers, as the cobble entry defines them; the stone entry's attributes were another shape's collection.
    material = applyAndDescribe(L"textures\\a\\cobble01.dds");
    EXPECT_EQ(material, nlohmann::json::parse(R"({
        "shader_type": "pbr", "multilayer": true, "coat_color": [0.1, 0.2, 0.3], "coat_specular_level": 0.5,
        "coat_roughness": 0.6, "coat_strength": 0.7, "coat_diffuse": true, "coat_parallax": true, "coat_normal": true,
        "inner_uv_scale": 4, "slot1": "pbr\\a\\cobble01.dds", "slot2": "pbr\\a\\cobble01_n.dds",
        "slot6": "pbr\\a\\cobble01_rmaos.dds", "slot7": "pbr\\a\\cobble01_cnr.dds",
        "slot8": "pbr\\a\\cobble01_s.dds"})"));

    // Glint.
    material = applyAndDescribe(L"textures\\a\\exact.dds");
    EXPECT_EQ(material["glint"], nlohmann::json::parse(R"({"screen_space_scale": 1.5, "log_microfacet_density": 2.5,
        "microfacet_roughness": 0.3, "density_randomization": 0.4})"));
    EXPECT_FALSE(material.contains("subsurface"));

    // Hair and fuzz both, as the fuzzy entry defines them, with its slot lock; its slot8 shows in the textures.
    material = applyAndDescribe(L"textures\\a\\fuzzy.dds");
    EXPECT_EQ(material["hair"], true);
    EXPECT_EQ(material["fuzz"], nlohmann::json::parse(R"({"color": [0.1, 0.2, 0.3], "weight": 0.9, "texture": true})"));
    EXPECT_EQ(material["lock_rmaos"], true);
    EXPECT_EQ(material["slot8"], "custom\\sub_s.dds");
}

TEST_F(PatcherMeshShaderTruePBRTest,
       AppliedAttributesCollectPerShapeWhileTheyLive)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderTruePBR patcher(meshPath, &nif);
    auto* const other = m_builder.addShape("Other", slots(L"textures\\c\\metal.dds"));
    const auto apply = [&](nifly::NiShape& shape, const std::wstring& diffuse) {
        auto patched = slots(diffuse);
        const auto matches = matchesFor(patcher, patched);
        ASSERT_FALSE(matches.empty()) << TestUtil::pathToUTF8(diffuse);
        patcher.applyPatch(patched, shape, matches.back());
    };

    // Nothing is collected without a collector.
    apply(*m_shape, L"textures\\c\\metal.dds");

    const PatcherMeshShaderTruePBR::AppliedAttributes outer;
    EXPECT_EQ(outer.of(*m_shape), nullptr);
    {
        // The defaults of a file are attributes of its entries, and every applied entry adds its own, the last value
        // of an attribute winning.
        const PatcherMeshShaderTruePBR::AppliedAttributes inner;
        apply(*m_shape, L"textures\\c\\metal.dds");
        const auto* const attributes = inner.of(*m_shape);
        ASSERT_NE(attributes, nullptr);
        EXPECT_EQ(attributes->at("specular_level"), 0.2);
        EXPECT_EQ(attributes->at("parallax"), false); // from the defaults
        EXPECT_FALSE(attributes->contains("multilayer"));
        apply(*m_shape, L"textures\\a\\cobble01.dds");
        EXPECT_EQ(attributes->at("multilayer"), true);
        EXPECT_EQ(attributes->at("specular_level"), 0.2); // the cobble entry does not set it

        // Every entry asks for its shader again, so the multilayer of the earlier entry is gone after the metal.
        apply(*m_shape, L"textures\\c\\metal.dds");
        EXPECT_FALSE(attributes->contains("multilayer"));
        EXPECT_EQ(attributes->at("coat_strength"), 0.7); // a parameter stays as the cobble entry set it
        EXPECT_EQ(outer.of(*m_shape), nullptr); // the inner one collects while it lives

        // A shape that is PBR on disk and matched by no entry gets a match without entries, which applies none.
        auto prePatched = slots(L"textures\\a\\rmaos.dds");
        prePatched[5] = L"textures\\a\\rmaos_rmaos.dds";
        const auto matches = matchesFor(patcher, prePatched);
        ASSERT_EQ(matches.size(), 1U);
        patcher.applyPatch(prePatched, *other, matches[0]);
        EXPECT_EQ(inner.of(*other), nullptr);
    }

    // The outer one collects again once the inner one is gone.
    apply(*other, L"textures\\c\\metal.dds");
    ASSERT_NE(outer.of(*other), nullptr);
    EXPECT_EQ(outer.of(*other)->at("specular_level"), 0.2);
    EXPECT_EQ(outer.of(*m_shape), nullptr);
}

TEST_F(PatcherMeshShaderTruePBRTest,
       MaterialSaysWhatTheSequenceOfEntriesDid)
{
    // Each case is a config whose entries all match the stone, applied in file order to a stone shape of its own.
    PatcherMeshShaderTruePBR::loadOptions(false, false);
    auto& nif = m_builder.nif();
    PatcherMeshShaderTruePBR patcher(meshPath, &nif);
    const auto stone = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    unsigned cases = 0;
    const auto describe = [&](const std::string& config, bool hasAlphaProperty = false, bool hasVertexColors = false) {
        const auto name = L"pbrnifpatcher\\case" + std::to_wstring(++cases) + L".json";
        TestUtil::writeFile(dataDir() / name, config);
        m_pgd->populateFileMap(false, false);
        PatcherMeshShaderTruePBR::loadStatics({ name }, false);

        auto* const shape = m_builder.addShape("Case" + std::to_string(cases), stone);
        if (hasAlphaProperty)
            m_builder.addAlphaProperty(shape);
        shape->SetVertexColors(hasVertexColors);

        const PatcherMeshShaderTruePBR::AppliedAttributes applied;
        auto patched = stone;
        const auto matches = matchesFor(patcher, patched);
        EXPECT_EQ(matches.size(), 1U) << config;
        if (!matches.empty())
            patcher.applyPatch(patched, *shape, matches[0]);
        const auto* const attributes = applied.of(*shape);
        EXPECT_NE(attributes, nullptr) << config;
        return attributes ? PatcherMeshShaderTruePBR::material(nif, *shape, patched, *attributes) : nlohmann::json();
    };

    // A later value the patch ignores leaves the earlier one it applied.
    auto material = describe(R"([{"texture": "a\\stone", "specular_level": 0.5, "zbuffer_write": false},
                                 {"texture": "a\\stone", "specular_level": "x", "zbuffer_write": "no"}])");
    EXPECT_EQ(material["specular_level"], 0.5);
    EXPECT_EQ(material["zbuffer_write"], false);

    // The coat attributes count for a multilayer entry only, and the shaders go multilayer, glint, fuzz.
    material = describe(R"([{"texture": "a\\stone", "coat_strength": 0.7, "coat_normal": true},
                            {"texture": "a\\stone", "multilayer": true}])");
    EXPECT_EQ(material["multilayer"], true);
    EXPECT_FALSE(material.contains("coat_strength"));
    EXPECT_FALSE(material.contains("coat_normal"));
    material = describe(R"([{"texture": "a\\stone", "multilayer": true, "glint": {}, "fuzz": {}}])");
    EXPECT_FALSE(material.contains("glint"));
    EXPECT_FALSE(material.contains("fuzz"));
    material = describe(R"([{"texture": "a\\stone", "glint": {"screen_space_scale": 1}, "fuzz": {"weight": 1}}])");
    EXPECT_EQ(material["glint"], nlohmann::json::parse(R"({"screen_space_scale": 1})"));
    EXPECT_FALSE(material.contains("fuzz"));

    // A slot attribute the entry locks does nothing; a later emissive decides its lock again.
    material = describe(R"([{"texture": "a\\stone", "cubemap": "c.dds", "lock_cubemap": true, "parallax": true,
                             "lock_parallax": true}])");
    EXPECT_FALSE(material.contains("cubemap"));
    EXPECT_FALSE(material.contains("parallax"));
    EXPECT_EQ(material["lock_cubemap"], true);
    material = describe(R"([{"texture": "a\\stone", "emissive": true, "lock_emissive": true},
                            {"texture": "a\\stone", "emissive": true}])");
    EXPECT_FALSE(material.contains("lock_emissive"));
    material = describe(R"([{"texture": "a\\stone", "emissive": true, "lock_emissive": true},
                            {"texture": "a\\stone", "specular_level": 1}])");
    EXPECT_EQ(material["lock_emissive"], true);

    // The alpha property: the booleans act only when on, a delete drops what came before it and a create after it
    // drops the delete, and the values of a shape that has no property and gets none do nothing.
    material = describe(R"({"default": {"alphaprop_create": false}, "entries": [
                            {"texture": "a\\stone", "alphaprop_create": true, "alphaprop_thresh": 64},
                            {"texture": "a\\stone"}]})");
    EXPECT_EQ(material["alphaprop_create"], true);
    EXPECT_EQ(material["alphaprop_thresh"], 64);
    material = describe(R"([{"texture": "a\\stone", "alphaprop_delete": true},
                            {"texture": "a\\stone", "alphaprop_create": true}])",
                        true);
    EXPECT_EQ(material["alphaprop_create"], true);
    EXPECT_FALSE(material.contains("alphaprop_delete"));
    material = describe(R"([{"texture": "a\\stone", "alphaprop_flags": 4845},
                            {"texture": "a\\stone", "alphaprop_create": true}])");
    EXPECT_EQ(material["alphaprop_create"], true);
    EXPECT_FALSE(material.contains("alphaprop_flags"));
    material = describe(R"([{"texture": "a\\stone", "alphaprop_create": true, "alphaprop_flags": 4845},
                            {"texture": "a\\stone", "alphaprop_delete": true}])");
    EXPECT_EQ(material["alphaprop_delete"], true);
    EXPECT_FALSE(material.contains("alphaprop_create"));
    EXPECT_FALSE(material.contains("alphaprop_flags"));

    // Two attributes of one shader field say the value the field has, whichever applies.
    material = describe(R"([{"texture": "a\\stone", "multilayer": true, "coat_strength": 0.7, "coat_color": [1, 0, 0]},
                            {"texture": "a\\stone", "multilayer": true, "subsurface_opacity": 0.3,
                             "subsurface_color": [0, 1, 0]}])");
    EXPECT_EQ(material["coat_strength"], 0.3);
    EXPECT_EQ(material["subsurface_opacity"], 0.3);
    EXPECT_EQ(material["coat_color"], nlohmann::json::array({ 0, 1, 0 }));
    material = describe(R"([{"texture": "a\\stone", "subsurface_opacity": 0.3},
                            {"texture": "a\\stone", "multilayer": true, "coat_strength": 0.7}])");
    EXPECT_EQ(material["subsurface_opacity"], 0.7);
    EXPECT_EQ(material["coat_strength"], 0.7);

    // The vertex color multipliers count where the patch changed the colors: not on a shape without them, on one the
    // entry gives them. A single multiplier is written as the config gives it, a product past the range of a float
    // not at all.
    material = describe(R"([{"texture": "a\\stone", "vertex_color_lum_mult": 0.5}])");
    EXPECT_FALSE(material.contains("vertex_color_lum_mult"));
    material = describe(R"([{"texture": "a\\stone", "vertex_colors": true, "vertex_color_lum_mult": 0.5}])");
    EXPECT_EQ(material["vertex_color_lum_mult"], 0.5);
    material = describe(R"([{"texture": "a\\stone", "vertex_color_sat_mult": 2}])", false, true);
    EXPECT_EQ(material["vertex_color_sat_mult"].dump(), "2");
    material = describe(R"([{"texture": "a\\stone", "vertex_color_lum_mult": 1e30},
                            {"texture": "a\\stone", "vertex_color_lum_mult": 1e30}])",
                        false,
                        true);
    EXPECT_FALSE(material.contains("vertex_color_lum_mult"));
}

TEST_F(PatcherMeshShaderTruePBRTest,
       MaterialWritesTheGeometryAttributesThePatchUsed)
{
    // Two entries of one file apply to the stone in order: the later smoothing angle replaces the earlier, the vertex
    // color multipliers compound, a value that is not a number applies nothing, and the later entry's uv_scale, which
    // the patch applies after its auto_uv, replaces the earlier auto_uv.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\geometry.json",
                        R"([{"texture": "a\\stone", "smooth_angle": 30, "vertex_color_lum_mult": 0.5,
                             "vertex_color_sat_mult": 0.8, "auto_uv": 2},
                            {"texture": "a\\stone", "smooth_angle": 60, "vertex_color_lum_mult": 0.5,
                             "vertex_color_sat_mult": "more", "auto_uv": 4, "uv_scale": 3}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\geometry.json" }, false);

    auto& nif = m_builder.nif();
    PatcherMeshShaderTruePBR patcher(meshPath, &nif);
    m_shape->SetVertexColors(true);
    const auto stone = slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    // Applies the entries of the one match of the stone and describes the shape with what they define.
    const auto applyAndDescribe = [&] {
        const PatcherMeshShaderTruePBR::AppliedAttributes applied;
        auto patched = stone;
        const auto matches = matchesFor(patcher, patched);
        EXPECT_EQ(matches.size(), 1U);
        if (!matches.empty())
            patcher.applyPatch(patched, *m_shape, matches[0]);
        const auto* const attributes = applied.of(*m_shape);
        EXPECT_NE(attributes, nullptr);
        return attributes ? PatcherMeshShaderTruePBR::material(nif, *m_shape, patched, *attributes) : nlohmann::json();
    };

    auto material = applyAndDescribe();
    EXPECT_EQ(material.at("smooth_angle"), 60);
    EXPECT_EQ(material.at("vertex_color_lum_mult"), 0.25);
    EXPECT_EQ(material.at("vertex_color_sat_mult"), 0.8);
    EXPECT_EQ(material.at("vertex_color_sat_mult").dump(), "0.8"); // the float the patch used, written short
    EXPECT_EQ(material.at("uv_scale"), 3);
    EXPECT_FLOAT_EQ(m_shader->uvScale.u, 3);

    // An auto_uv after the last uv_scale writes the scale it computed for both directions.
    TestUtil::writeFile(dataDir() / L"pbrnifpatcher\\uv.json",
                        R"([{"texture": "a\\stone", "uv_scale": 3},
                            {"texture": "a\\stone", "auto_uv": 2, "uv_scale": "large"}])");
    m_pgd->populateFileMap(false, false);
    PatcherMeshShaderTruePBR::loadStatics({ L"pbrnifpatcher\\uv.json" }, false);
    material = applyAndDescribe();
    EXPECT_GT(m_shader->uvScale.u, 1);
    EXPECT_NE(m_shader->uvScale.u, 3);
    EXPECT_FLOAT_EQ(material.at("uv_scale").get<float>(), m_shader->uvScale.u);
    EXPECT_FALSE(material.contains("auto_uv"));

    // Values that are not numbers are left out, as the patch ignores them.
    nlohmann::json attributes = nlohmann::json::object();
    attributes["smooth_angle"] = false;
    attributes["vertex_color_lum_mult"] = "half";
    const auto ignored = PatcherMeshShaderTruePBR::material(nif, *m_shape, stone, attributes);
    EXPECT_FALSE(ignored.contains("smooth_angle"));
    EXPECT_FALSE(ignored.contains("vertex_color_lum_mult"));
}
}
