#include "PGDirectory.hpp"

#include "BSABuilder.hpp"
#include "FakeGame.hpp"
#include "NIFBuilder.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/ExceptionHandler.hpp"

#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureSlots;
using PGEnums::TextureType;
using TestUtil::FakeGame;
using TestUtil::NIFBuilder;

PGTypes::TextureSet slots(const std::vector<std::pair<unsigned,
                                                      std::wstring>>& textures)
{
    PGTypes::TextureSet set;
    for (const auto& [slot, texture] : textures)
        set.at(slot) = texture;
    return set;
}

/**
 * @brief Writes a single shape mesh with the given shader setup into the data folder.
 */
void writeMesh(const FakeGame& fake,
               const std::wstring& relPath,
               const PGTypes::TextureSet& textures,
               nifly::BSLightingShaderPropertyShaderType shaderType = nifly::BSLSP_DEFAULT,
               uint32_t flags1 = 0,
               uint32_t flags2 = 0)
{
    NIFBuilder builder;
    auto* shader = builder.lightingShader(builder.addShape("Shape", textures, shaderType));
    shader->shaderFlags1 |= flags1;
    shader->shaderFlags2 |= flags2;
    builder.save(fake.dataDir() / relPath);
}

/**
 * @brief A data folder exercising every texture classification rule, mapped by a PGDirectory.
 */
class PGDirectoryTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    FakeGame m_fake;
    std::optional<BethesdaGame> m_game;
    std::unique_ptr<PGDirectory> m_pgd;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* vanillaArchive = L"Vanilla.bsa";

    void SetUp() override
    {
        ExceptionHandler::reset();

        // Textures; their content does not matter for the mapping.
        for (const auto* texture : {
                 L"textures\\a\\stone.dds",   L"textures\\a\\stone_n.dds",    L"textures\\a\\stone_p.dds",
                 L"textures\\a\\stone_m.dds", L"textures\\cubemaps\\c_e.dds", L"textures\\a\\lamp_g.dds",
                 L"textures\\pbr\\x_g.dds",   L"textures\\pbr\\x_p.dds",      L"textures\\pbr\\x_rmaos.dds",
                 L"textures\\a\\ml_s.dds",    L"textures\\a\\ml_i.dds",       L"textures\\a\\coat_cnr.dds",
                 L"textures\\a\\sub_sss.dds", L"textures\\a\\hair_flow.dds",  L"textures\\a\\skin_msn.dds",
                 L"textures\\a\\skin_sk.dds", L"textures\\a\\skin_s.dds",     L"textures\\a\\x_b.dds",
                 L"textures\\a\\dual.dds",    L"textures\\a\\plain.dds",      L"textures\\a\\orphan_n.dds",
                 L"textures\\a\\orphan.dds",  L"textures\\a\\manual.dds",     L"textures\\blocked\\b_n.dds",
             }) {
            m_fake.writeDataFile(texture, "DDS");
        }
        m_fake.writeDataFile(L"textures\\a\\café.dds", "DDS");

        writeMesh(m_fake,
                  L"meshes\\a\\stone.nif",
                  slots({ { 0, L"textures\\a\\stone.dds" }, { 1, L"textures\\a\\stone_n.dds" } }));
        writeMesh(m_fake,
                  L"meshes\\a\\parallax.nif",
                  slots({
                      { 0, L"textures\\a\\stone.dds" },
                      { 1, L"textures\\a\\stone_n.dds" },
                      { 3, L"textures\\a\\stone_p.dds" },
                  }),
                  nifly::BSLSP_PARALLAX,
                  nifly::SLSF1_PARALLAX);
        writeMesh(m_fake,
                  L"meshes\\a\\envmap.nif",
                  slots({ { 4, L"textures\\cubemaps\\c_e.dds" }, { 5, L"textures\\a\\stone_m.dds" } }),
                  nifly::BSLSP_ENVMAP,
                  nifly::SLSF1_ENVIRONMENT_MAPPING);
        writeMesh(m_fake,
                  L"meshes\\a\\glow.nif",
                  slots({ { 2, L"textures\\a\\lamp_g.dds" } }),
                  nifly::BSLSP_GLOWMAP,
                  0,
                  nifly::SLSF2_GLOW_MAP);
        writeMesh(m_fake,
                  L"meshes\\a\\pbr.nif",
                  slots({
                      { 2, L"textures\\pbr\\x_g.dds" },
                      { 3, L"textures\\pbr\\x_p.dds" },
                      { 5, L"textures\\pbr\\x_rmaos.dds" },
                  }),
                  nifly::BSLSP_DEFAULT,
                  0,
                  nifly::SLSF2_UNUSED01);
        writeMesh(m_fake,
                  L"meshes\\a\\multilayer.nif",
                  slots({ { 2, L"textures\\a\\ml_s.dds" }, { 6, L"textures\\a\\ml_i.dds" } }),
                  nifly::BSLSP_MULTILAYERPARALLAX,
                  0,
                  nifly::SLSF2_MULTI_LAYER_PARALLAX);
        writeMesh(m_fake,
                  L"meshes\\a\\multilayer_pbr.nif",
                  slots({ { 6, L"textures\\a\\coat_cnr.dds" }, { 7, L"textures\\a\\sub_sss.dds" } }),
                  nifly::BSLSP_MULTILAYERPARALLAX,
                  0,
                  nifly::SLSF2_MULTI_LAYER_PARALLAX | nifly::SLSF2_UNUSED01);
        writeMesh(m_fake,
                  L"meshes\\a\\hair.nif",
                  slots({ { 7, L"textures\\a\\hair_flow.dds" } }),
                  nifly::BSLSP_HAIRTINT,
                  0,
                  nifly::SLSF2_BACK_LIGHTING);
        writeMesh(m_fake,
                  L"meshes\\a\\skin.nif",
                  slots({
                      { 1, L"textures\\a\\skin_msn.dds" },
                      { 2, L"textures\\a\\skin_sk.dds" },
                      { 7, L"textures\\a\\skin_s.dds" },
                  }),
                  nifly::BSLSP_SKINTINT,
                  nifly::SLSF1_FACEGEN_RGB_TINT);
        writeMesh(m_fake,
                  L"meshes\\a\\backlight.nif",
                  slots({ { 7, L"textures\\a\\x_b.dds" } }),
                  nifly::BSLSP_DEFAULT,
                  0,
                  nifly::SLSF2_BACK_LIGHTING);
        writeMesh(m_fake, L"meshes\\a\\dual1.nif", slots({ { 0, L"textures\\a\\dual.dds" } }));
        writeMesh(m_fake, L"meshes\\a\\dual2.nif", slots({ { 0, L"textures\\a\\dual.dds" } }));
        writeMesh(m_fake,
                  L"meshes\\a\\dual3.nif",
                  slots({ { 2, L"textures\\a\\dual.dds" } }),
                  nifly::BSLSP_GLOWMAP,
                  0,
                  nifly::SLSF2_GLOW_MAP);
        writeMesh(m_fake, L"meshes\\a\\unknown_slot.nif", slots({ { 2, L"textures\\a\\plain.dds" } }));
        writeMesh(m_fake, L"meshes\\a\\missing_ref.nif", slots({ { 0, L"textures\\a\\missing.dds" } }));
        writeMesh(m_fake, L"meshes\\blocked\\b.nif", slots({ { 0, L"textures\\blocked\\b_n.dds" } }));
        writeMesh(
            m_fake, L"calientetools\\bodyslide\\shapedata\\x\\y.nif", slots({ { 0, L"textures\\a\\stone.dds" } }));
        m_fake.writeDataFile(L"meshes\\broken.nif", "not a nif");
        m_fake.writeDataFile(L"meshes\\readme.txt", "ignored");
        m_fake.writeDataFile(L"textures\\stray.nif", "nif outside meshes");
        m_fake.writeDataFile(L"pbrnifpatcher\\cfg.json", "[]");
        m_fake.writeDataFile(L"lightplacer\\lp.json", "[]");

        // A "vanilla" archive whose height map and environment mask must not be taken for parallax textures.
        m_fake.addPlugin(L"Vanilla.esp");
        TestUtil::BSABuilder archive;
        archive.addFile("textures\\vanilla\\rock.dds", "DDS");
        archive.addFile("textures\\vanilla\\rock_p.dds", "DDS");
        archive.addFile("textures\\vanilla\\rock_m.dds", "DDS");
        archive.addFile("textures\\vanilla\\other_p.dds", "DDS");
        NIFBuilder rock;
        auto* rockShader = rock.lightingShader(
            rock.addShape("Rock",
                          slots({ { 0, L"textures\\vanilla\\rock.dds" }, { 3, L"textures\\vanilla\\rock_p.dds" } }),
                          nifly::BSLSP_PARALLAX));
        rockShader->shaderFlags1 |= nifly::SLSF1_PARALLAX;
        archive.addFile("meshes\\vanilla\\rock.nif", rock.bytes());
        archive.write(m_fake.dataDir() / vanillaArchive);
        // A loose height map in a mod folder that is not excluded.
        m_fake.writeDataFile(L"textures\\vanilla\\other_p.dds", "DDS");

        m_game.emplace(m_fake.game());
        m_pgd = std::make_unique<PGDirectory>(&*m_game, m_fake.gameDir() / L"out");
        m_pgd->populateFileMap(true, false);
    }

    void TearDown() override { ExceptionHandler::reset(); }

    void mapAll(bool multithreading,
                const std::vector<std::wstring>& blocklist = { L"meshes\\blocked\\*" },
                const std::vector<std::wstring>& allowlist = { })
    {
        m_pgd->mapFiles(blocklist,
                        allowlist,
                        { { L"textures\\a\\manual.dds", TextureType::ComplexMaterial } },
                        { vanillaArchive },
                        multithreading);
        m_pgd->waitForMeshMapping();
        m_pgd->waitForCMClassification();
    }

    static bool mapHas(const PGDirectory& pgd,
                       TextureSlots slot,
                       const std::wstring& base,
                       const std::wstring& texture,
                       TextureType type)
    {
        const auto& map = pgd.textureMapConst(slot);
        const auto it = map.find(base);
        if (it == map.end())
            return false;

        return it->second.contains(PGTypes::PGTexture { .path = texture, .type = type });
    }
};

TEST(PGDirectoryPlainTest,
     MapFilesNeedsAPopulatedFileMap)
{
    const TestUtil::TempDir dir;
    PGDirectory pgd(dir.path(), dir / L"out");
    EXPECT_THROW(pgd.mapFiles({ }, { }, { }, { }, false), std::runtime_error);
    EXPECT_TRUE(pgd.meshes().empty());
    EXPECT_TRUE(pgd.textures().empty());
    EXPECT_TRUE(pgd.pbrJSONs().empty());
    EXPECT_TRUE(pgd.lightPlacerJSONs().empty());
    EXPECT_EQ(pgd.textureType(L"textures\\a.dds"), TextureType::Unknown);
}

TEST(PGDirectoryPlainTest,
     CheckGlobMatchInVector)
{
    EXPECT_TRUE(PGDirectory::checkGlobMatchInVector(L"meshes\\armor\\iron.nif", { L"*.dds", L"meshes\\armor\\*" }));
    EXPECT_TRUE(PGDirectory::checkGlobMatchInVector(L"meshes\\armor\\iron.nif", { L"*iron*" }));
    EXPECT_FALSE(PGDirectory::checkGlobMatchInVector(L"meshes\\armor\\iron.nif", { L"*.dds" }));
    EXPECT_FALSE(PGDirectory::checkGlobMatchInVector(L"meshes\\armor\\iron.nif", { }));
}

TEST_F(PGDirectoryTest,
       FindsMeshesTexturesAndJsons)
{
    mapAll(false);

    const std::set<std::wstring> expectedMeshes {
        L"meshes\\a\\stone.nif",
        L"meshes\\a\\parallax.nif",
        L"meshes\\a\\envmap.nif",
        L"meshes\\a\\glow.nif",
        L"meshes\\a\\pbr.nif",
        L"meshes\\a\\multilayer.nif",
        L"meshes\\a\\multilayer_pbr.nif",
        L"meshes\\a\\hair.nif",
        L"meshes\\a\\skin.nif",
        L"meshes\\a\\backlight.nif",
        L"meshes\\a\\dual1.nif",
        L"meshes\\a\\dual2.nif",
        L"meshes\\a\\dual3.nif",
        L"meshes\\a\\unknown_slot.nif",
        L"meshes\\a\\missing_ref.nif",
        L"meshes\\vanilla\\rock.nif",
        L"calientetools\\bodyslide\\shapedata\\x\\y.nif",
    };
    std::set<std::wstring> meshes;
    for (const auto& [path, cache] : m_pgd->meshes()) {
        meshes.insert(path.wstring());
        EXPECT_TRUE(cache.meshUses.empty()); // no plugins are loaded
    }
    EXPECT_EQ(meshes, expectedMeshes);

    // The blocked mesh and the unreadable one are not mapped, the latter with an error.
    EXPECT_FALSE(m_pgd->meshes().contains(L"meshes\\blocked\\b.nif"));
    EXPECT_FALSE(m_pgd->meshes().contains(L"meshes\\broken.nif"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Unable to process mesh: meshes\\broken.nif"));

    EXPECT_TRUE(m_pgd->textures().contains(L"textures\\a\\stone.dds"));
    EXPECT_TRUE(m_pgd->textures().contains(L"textures\\vanilla\\rock_p.dds"));
    EXPECT_TRUE(m_pgd->textures().contains(L"textures\\a\\orphan.dds"));
    EXPECT_FALSE(m_pgd->textures().contains(L"textures\\a\\café.dds"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "contains non-ascii characters which are not allowed"));
    EXPECT_FALSE(m_pgd->textures().contains(L"textures\\stray.nif"));

    EXPECT_EQ(m_pgd->pbrJSONs(), (std::vector<std::filesystem::path> { L"pbrnifpatcher\\cfg.json" }));
    EXPECT_EQ(m_pgd->lightPlacerJSONs(), (std::vector<std::filesystem::path> { L"lightplacer\\lp.json" }));
}

TEST_F(PGDirectoryTest,
       ShapesVoteForTheTypeOfTheirTextures)
{
    mapAll(false);

    const std::vector<std::pair<std::wstring, TextureType>> expected {
        { L"textures\\a\\stone.dds", TextureType::Diffuse },
        { L"textures\\a\\stone_n.dds", TextureType::Normal },
        { L"textures\\a\\stone_p.dds", TextureType::Height },
        { L"textures\\cubemaps\\c_e.dds", TextureType::Cubemap },
        { L"textures\\a\\lamp_g.dds", TextureType::Emissive },
        { L"textures\\pbr\\x_g.dds", TextureType::Emissive },
        { L"textures\\pbr\\x_p.dds", TextureType::HeightPBR },
        { L"textures\\pbr\\x_rmaos.dds", TextureType::RMAOS },
        { L"textures\\a\\ml_s.dds", TextureType::SubsurfaceColor },
        { L"textures\\a\\ml_i.dds", TextureType::InnerLayer },
        { L"textures\\a\\coat_cnr.dds", TextureType::CoatNormalRoughness },
        { L"textures\\a\\sub_sss.dds", TextureType::SubsurfacePBR },
        { L"textures\\a\\hair_flow.dds", TextureType::HairFlowMap },
        { L"textures\\a\\skin_msn.dds", TextureType::ModelSpaceNormal },
        { L"textures\\a\\skin_sk.dds", TextureType::SkinTint },
        { L"textures\\a\\skin_s.dds", TextureType::Specular },
        { L"textures\\a\\x_b.dds", TextureType::Backlight },
        { L"textures\\a\\dual.dds", TextureType::Diffuse }, // two diffuse votes beat one emissive vote
        { L"textures\\a\\orphan_n.dds", TextureType::Normal }, // no votes: the suffix decides
        { L"textures\\blocked\\b_n.dds", TextureType::Normal }, // its mesh is blocked, so the suffix decides
        { L"textures\\a\\manual.dds", TextureType::ComplexMaterial }, // manual texture map
        { L"textures\\vanilla\\rock.dds", TextureType::Diffuse },
        { L"textures\\vanilla\\other_p.dds", TextureType::Height },
        { L"textures\\a\\plain.dds", TextureType::Unknown }, // glow slot without a glow shader casts no vote
        { L"textures\\a\\orphan.dds", TextureType::Unknown },
        { L"textures\\vanilla\\rock_p.dds", TextureType::Unknown }, // excluded archive
        { L"textures\\a\\missing.dds", TextureType::Unknown },
    };
    for (const auto& [texture, type] : expected)
        EXPECT_EQ(m_pgd->textureType(texture), type) << TestUtil::pathToUTF8(texture);

    // The texture maps are keyed by the base path of the slot.
    EXPECT_TRUE(
        mapHas(*m_pgd, TextureSlots::Diffuse, L"textures\\a\\stone", L"textures\\a\\stone.dds", TextureType::Diffuse));
    EXPECT_TRUE(
        mapHas(*m_pgd, TextureSlots::Normal, L"textures\\a\\stone", L"textures\\a\\stone_n.dds", TextureType::Normal));
    EXPECT_TRUE(mapHas(
        *m_pgd, TextureSlots::Parallax, L"textures\\a\\stone", L"textures\\a\\stone_p.dds", TextureType::Height));
    EXPECT_TRUE(
        mapHas(*m_pgd, TextureSlots::Glow, L"textures\\a\\lamp", L"textures\\a\\lamp_g.dds", TextureType::Emissive));
    EXPECT_TRUE(
        mapHas(*m_pgd, TextureSlots::EnvMask, L"textures\\pbr\\x", L"textures\\pbr\\x_rmaos.dds", TextureType::RMAOS));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::EnvMask,
                       L"textures\\a\\manual",
                       L"textures\\a\\manual.dds",
                       TextureType::ComplexMaterial));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::Backlight,
                       L"textures\\a\\hair",
                       L"textures\\a\\hair_flow.dds",
                       TextureType::HairFlowMap));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::Parallax,
                       L"textures\\vanilla\\other",
                       L"textures\\vanilla\\other_p.dds",
                       TextureType::Height));
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::Parallax).contains(L"textures\\vanilla\\rock"));
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::Glow).contains(L"textures\\a\\plain"));

    // Environment masks need the GPU for their classification, which is not available here.
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_m.dds"), TextureType::Unknown);
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Unable to process texture: textures\\a\\stone_m.dds"));
    // Unless the texture comes from an excluded archive, then it is a plain environment mask.
    EXPECT_EQ(m_pgd->textureType(L"textures\\vanilla\\rock_m.dds"), TextureType::EnvironmentMask);
}

TEST_F(PGDirectoryTest,
       MultithreadedMappingGivesTheSameResult)
{
    mapAll(true);

    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_p.dds"), TextureType::Height);
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\dual.dds"), TextureType::Diffuse);
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\skin_s.dds"), TextureType::Specular);
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\manual.dds"), TextureType::ComplexMaterial);
    EXPECT_EQ(m_pgd->meshes().size(), 17U);
    EXPECT_TRUE(
        mapHas(*m_pgd, TextureSlots::Normal, L"textures\\a\\stone", L"textures\\a\\stone_n.dds", TextureType::Normal));

    // Waiting again is harmless.
    m_pgd->waitForMeshMapping();
    m_pgd->waitForCMClassification();
}

TEST_F(PGDirectoryTest,
       AllowlistRestrictsTheMappedMeshes)
{
    std::vector<std::pair<size_t, size_t>> progress;
    m_pgd->mapFiles(
        { }, { L"meshes\\a\\stone.nif", L"meshes\\a\\dual*" }, { }, { }, false, [&progress](size_t done, size_t total) {
            progress.emplace_back(done, total);
        });
    m_pgd->waitForMeshMapping();

    std::set<std::wstring> meshes;
    for (const auto& [path, cache] : m_pgd->meshes())
        meshes.insert(path.wstring());
    EXPECT_EQ(
        meshes,
        (std::set<std::wstring> {
            L"meshes\\a\\stone.nif", L"meshes\\a\\dual1.nif", L"meshes\\a\\dual2.nif", L"meshes\\a\\dual3.nif" }));

    // Skipped meshes still count for the progress, which covers every mesh found: the 15 in meshes\a, the blocked
    // one, the BodySlide one, the broken one and the archived one.
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back().first, progress.back().second);
    EXPECT_EQ(progress.back().second, 19U);

    // Without the parallax mesh the height map is only known by its suffix.
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_p.dds"), TextureType::Height);
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\skin_s.dds"), TextureType::SubsurfaceTint); // suffix, not the vote
}

TEST_F(PGDirectoryTest,
       TextureAttributesAndTypesCanBeEdited)
{
    mapAll(false);
    const std::filesystem::path known = L"textures\\a\\stone.dds";
    const std::filesystem::path unknown = L"textures\\a\\nothing.dds";

    EXPECT_TRUE(m_pgd->textureAttributes(known).empty());
    EXPECT_FALSE(m_pgd->hasTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_TRUE(m_pgd->addTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_FALSE(m_pgd->addTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_TRUE(m_pgd->addTextureAttribute(known, PGEnums::TextureAttribute::CMGlossiness));
    EXPECT_TRUE(m_pgd->hasTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_EQ(m_pgd->textureAttributes(known).size(), 2U);
    EXPECT_TRUE(m_pgd->removeTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_FALSE(m_pgd->removeTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_FALSE(m_pgd->hasTextureAttribute(known, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_TRUE(m_pgd->hasTextureAttribute(known, PGEnums::TextureAttribute::CMGlossiness));

    // Unknown textures take no attributes.
    EXPECT_FALSE(m_pgd->addTextureAttribute(unknown, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_FALSE(m_pgd->removeTextureAttribute(unknown, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_FALSE(m_pgd->hasTextureAttribute(unknown, PGEnums::TextureAttribute::CMEnvMask));
    EXPECT_TRUE(m_pgd->textureAttributes(unknown).empty());

    // Setting a type creates the entry.
    EXPECT_EQ(m_pgd->textureType(unknown), TextureType::Unknown);
    m_pgd->setTextureType(unknown, TextureType::Height);
    EXPECT_EQ(m_pgd->textureType(unknown), TextureType::Height);
    EXPECT_TRUE(m_pgd->addTextureAttribute(unknown, PGEnums::TextureAttribute::CMHeight));
    m_pgd->setTextureType(known, TextureType::Emissive);
    EXPECT_EQ(m_pgd->textureType(known), TextureType::Emissive);
}

TEST_F(PGDirectoryTest,
       TextureMapIsMutable)
{
    mapAll(false);
    auto& map = m_pgd->textureMap(TextureSlots::Parallax);
    map[L"textures\\new\\thing"].insert({ .path = L"textures\\new\\thing_p.dds", .type = TextureType::Height });
    EXPECT_TRUE(mapHas(
        *m_pgd, TextureSlots::Parallax, L"textures\\new\\thing", L"textures\\new\\thing_p.dds", TextureType::Height));
    EXPECT_EQ(&m_pgd->textureMapConst(TextureSlots::Parallax), &map);
}

TEST(PGDirectoryPlainTest,
     MapTexturesBySuffixClassifiesWithoutReadingMeshes)
{
    FakeGame fake;
    for (const auto* texture : {
             L"textures\\a\\stone.dds",
             L"textures\\a\\stone_n.dds",
             L"textures\\a\\stone_p.dds",
             L"textures\\a\\stone_m.dds",
         })
        fake.writeDataFile(texture, "DDS");
    fake.writeMainINI("[Archive]\nsResourceArchiveList=Vanilla.bsa\n");
    TestUtil::BSABuilder vanilla;
    vanilla.addFile("textures\\v\\rock_p.dds", "DDS");
    vanilla.write(fake.dataDir() / L"Vanilla.bsa");

    // A mesh whose shape would cast a vote. It is neither mapped nor read.
    writeMesh(fake, L"meshes\\a\\stone.nif", slots({ { 0, L"textures\\a\\stone.dds" } }));

    BethesdaGame game = fake.game();
    PGDirectory pgd(&game);
    pgd.setFoldersToMap({ L"textures" });
    EXPECT_THROW(pgd.mapTexturesBySuffix({ }, { }), std::runtime_error);
    pgd.populateFileMap(true, false);
    pgd.mapTexturesBySuffix({ { L"textures\\a\\stone_m.dds", TextureType::ComplexMaterial } }, { L"Vanilla.bsa" });

    EXPECT_TRUE(pgd.meshes().empty());
    EXPECT_FALSE(pgd.isFile(L"meshes\\a\\stone.nif"));
    EXPECT_EQ(pgd.textures().size(), 5U);

    const auto& normals = pgd.textureMapConst(TextureSlots::Normal);
    ASSERT_TRUE(normals.contains(L"textures\\a\\stone"));
    EXPECT_EQ(normals.at(L"textures\\a\\stone").begin()->path, L"textures\\a\\stone_n.dds");
    const auto& heights = pgd.textureMapConst(TextureSlots::Parallax);
    EXPECT_TRUE(heights.contains(L"textures\\a\\stone"));
    EXPECT_FALSE(heights.contains(L"textures\\v\\rock")); // height map of a vanilla archive
    // A diffuse texture has no suffix, so without a mesh vote it stays unclassified, as offline.
    EXPECT_EQ(pgd.textureType(L"textures\\a\\stone.dds"), TextureType::Unknown);
    EXPECT_EQ(pgd.textureType(L"textures\\a\\stone_m.dds"), TextureType::ComplexMaterial);
    EXPECT_EQ(pgd.textureType(L"textures\\v\\rock_p.dds"), TextureType::Unknown);
}
}
