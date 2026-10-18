#include "PGDirectory.hpp"

#include "BSABuilder.hpp"
#include "DDSBuilder.hpp"
#include "FakeGame.hpp"
#include "NIFBuilder.hpp"
#include "PGD3D.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/Logger.hpp"

#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
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
using TestUtil::DDSBuilder::RGBA;

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
 * @brief Whether the texture map of a slot lists a texture of the given type under a base path.
 */
bool mapHas(const PGDirectory& pgd,
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
       ExcludedFilesStayOutOfTheMapLooseOrArchived)
{
    PGDirectory pgd(&*m_game, m_fake.gameDir() / L"out2");
    pgd.setExcludedFiles({ L"textures\\a\\stone_m.dds", L"textures\\vanilla\\rock_m.dds" });
    pgd.populateFileMap(true, false);

    EXPECT_FALSE(pgd.isFile(L"textures\\a\\stone_m.dds")); // loose
    EXPECT_FALSE(pgd.isFile(L"textures\\vanilla\\rock_m.dds")); // archived
    EXPECT_TRUE(pgd.isFile(L"textures\\a\\stone.dds"));
    EXPECT_TRUE(pgd.isFile(L"textures\\vanilla\\rock.dds"));

    // The fixture's own directory, without exclusions, has them.
    EXPECT_TRUE(m_pgd->isFile(L"textures\\a\\stone_m.dds"));
    EXPECT_TRUE(m_pgd->isFile(L"textures\\vanilla\\rock_m.dds"));
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

TEST_F(PGDirectoryTest,
       TextureRulesMatchWhateverTheirCaseAndSlashes)
{
    // The rule as a user types it in a dialog, against the mapped path (lower case, backslashes).
    m_pgd->mapFiles({ L"meshes\\blocked\\*" },
                    { },
                    { { L"Textures/A/Manual.DDS", TextureType::ComplexMaterial } },
                    { vanillaArchive },
                    false);
    m_pgd->waitForMeshMapping();
    m_pgd->waitForCMClassification();
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\manual.dds"), TextureType::ComplexMaterial);
}

// Warnings and errors handed to the thread capture callback, which has to be a plain function pointer. The capture sees
// every message, also one that Logger suppresses because it was logged before in the process.
std::vector<std::pair<spdlog::level::level_enum, std::wstring>>
    s_captured; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void captureCallback(spdlog::level::level_enum level,
                     const std::wstring& message)
{
    s_captured.emplace_back(level, message);
}

/**
 * @brief Whether a captured message of the given level contains needle.
 */
bool wasCaptured(spdlog::level::level_enum level,
                 const std::wstring& needle)
{
    return std::ranges::any_of(s_captured, [&level, &needle](const auto& entry) {
        return entry.first == level && entry.second.find(needle) != std::wstring::npos;
    });
}

/**
 * @brief Captures the warnings and errors that this thread logs while alive.
 */
class ThreadCapture {
public:
    ThreadCapture()
    {
        s_captured.clear();
        Logger::setThreadMessageCapture(&captureCallback);
    }

    ~ThreadCapture() { Logger::setThreadMessageCapture(nullptr); }

    ThreadCapture(const ThreadCapture&) = delete;
    ThreadCapture& operator=(const ThreadCapture&) = delete;
    ThreadCapture(ThreadCapture&&) = delete;
    ThreadCapture& operator=(ThreadCapture&&) = delete;
};

/**
 * @brief An observer that answers every complex material classification from its cache, the way PGLite's
 * classification cache and the update cache answer for the textures they classified before, and keeps the textures it
 * was asked for.
 */
class CachedClassificationObserver final : public PGRunObserver {
public:
    // The test reads the questions directly.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    std::vector<std::filesystem::path> questions;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    bool tryGetCachedCMClassification(const std::filesystem::path& texture,
                                      [[maybe_unused]] const BethesdaDirectory::FileIdentity& identity,
                                      PGTypes::CMClassification& result) override
    {
        questions.push_back(texture);
        result = { .isCM = true, .hasEnvMask = true, .hasGlossiness = false, .hasMetalness = true };
        return true;
    }
};

/**
 * @brief Installs an observer while alive.
 */
class ScopedObserver {
public:
    explicit ScopedObserver(PGRunObserver& observer) { PGRunObserver::install(&observer); }

    ~ScopedObserver() { PGRunObserver::install(nullptr); }

    ScopedObserver(const ScopedObserver&) = delete;
    ScopedObserver& operator=(const ScopedObserver&) = delete;
    ScopedObserver(ScopedObserver&&) = delete;
    ScopedObserver& operator=(ScopedObserver&&) = delete;
};

/**
 * @brief A data folder whose height maps and environment masks have JSONs next to them, loose and archived, with
 * meshes whose shapes decide the type of some of them, mapped by a PGDirectory.
 */
class PGDirectoryTextureJSONTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    FakeGame m_fake;
    std::optional<BethesdaGame> m_game;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d;
    std::size_t m_logStart = 0; /** Number of log entries before the last mapAll() */
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const char* disabledJSON = R"({"disabled": true})";

    void SetUp() override
    {
        ExceptionHandler::reset();

        // Nothing that an earlier test captured or logged counts for this one.
        s_captured.clear();
        m_logStart = TestLog::entries().size();

        // Textures; only the complex material classification reads one.
        for (const auto* texture : {
                 L"textures\\json\\off_p.dds",       L"textures\\json\\on_p.dds",
                 L"textures\\json\\nokey_p.dds",     L"textures\\json\\keycase_p.dds",
                 L"textures\\json\\string_p.dds",    L"textures\\json\\array_p.dds",
                 L"textures\\json\\broken_p.dds",    L"textures\\json\\upper_p.dds",
                 L"textures\\json\\ruled_p.dds",     L"textures\\json\\ruledbroken_p.dds",
                 L"textures\\json\\ruledcm_m.dds",   L"textures\\json\\plain_p.dds",
                 L"textures\\json\\off_m.dds",       L"textures\\json\\wall_n.dds",
                 L"textures\\json\\voted.dds",       L"textures\\json\\voted_d.dds",
                 L"textures\\json\\envvote.dds",     L"textures\\json\\pbrvote_p.dds",
                 L"textures\\json\\rmaosvote_m.dds", L"textures\\json\\diffvote_p.dds",
                 L"textures\\pbr\\json\\x_p.dds",
             }) {
            m_fake.writeDataFile(texture, "DDS");
        }
        // Two equal complex material textures: transparent, with the environment mask and metalness channels in use.
        for (const auto* texture : { L"textures\\json\\cm_m.dds", L"textures\\json\\cmoff_m.dds" }) {
            TestUtil::DDSBuilder::writeDDS(
                m_fake.dataDir() / texture, 16, 16, RGBA { .r = 255, .g = 0, .b = 100, .a = 0 });
        }

        for (const auto& [path, content] : std::vector<std::pair<std::wstring, std::string>> {
                 { L"textures\\json\\off_p.json", disabledJSON },
                 { L"textures\\json\\on_p.json", R"({"disabled": false})" },
                 { L"textures\\json\\nokey_p.json", R"({"$schema": "parallax.schema.json"})" },
                 { L"textures\\json\\keycase_p.json", R"({"Disabled": true})" },
                 { L"textures\\json\\string_p.json", R"({"disabled": "true"})" },
                 { L"textures\\json\\array_p.json", R"([{"disabled": true}])" },
                 { L"textures\\json\\broken_p.json", R"({"disabled": true)" },
                 { L"textures\\json\\Upper_P.JSON", disabledJSON },
                 { L"textures\\json\\ruled_p.json", disabledJSON },
                 { L"textures\\json\\ruledbroken_p.json", R"({"disabled": true)" },
                 { L"textures\\json\\ruledcm_m.json", disabledJSON },
                 { L"textures\\json\\off_m.json", disabledJSON },
                 { L"textures\\json\\cmoff_m.json", disabledJSON },
                 { L"textures\\json\\wall_n.json", disabledJSON },
                 { L"textures\\json\\voted.json", disabledJSON },
                 { L"textures\\json\\envvote.json", disabledJSON },
                 { L"textures\\json\\pbrvote_p.json", disabledJSON },
                 { L"textures\\json\\rmaosvote_m.json", disabledJSON },
                 { L"textures\\json\\diffvote_p.json", disabledJSON },
                 { L"textures\\pbr\\json\\x_p.json", disabledJSON },
             }) {
            m_fake.writeDataFile(path, content);
        }

        // Shapes decide the type before the file name suffix does: a parallax shape makes a height map and an
        // environment map shape an environment mask of a texture without the suffix, a TruePBR shape makes a TruePBR
        // height map of a _p texture and an RMAOS map of a _m texture, and a diffuse slot makes a diffuse map.
        writeMesh(m_fake,
                  L"meshes\\json\\parallax.nif",
                  slots({ { 0, L"textures\\json\\voted_d.dds" }, { 3, L"textures\\json\\voted.dds" } }),
                  nifly::BSLSP_PARALLAX,
                  nifly::SLSF1_PARALLAX);
        writeMesh(m_fake,
                  L"meshes\\json\\envmap.nif",
                  slots({ { 5, L"textures\\json\\envvote.dds" } }),
                  nifly::BSLSP_ENVMAP,
                  nifly::SLSF1_ENVIRONMENT_MAPPING);
        writeMesh(m_fake,
                  L"meshes\\json\\pbr.nif",
                  slots({ { 3, L"textures\\json\\pbrvote_p.dds" }, { 5, L"textures\\json\\rmaosvote_m.dds" } }),
                  nifly::BSLSP_DEFAULT,
                  0,
                  nifly::SLSF2_UNUSED01);
        writeMesh(m_fake, L"meshes\\json\\diffuse.nif", slots({ { 0, L"textures\\json\\diffvote_p.dds" } }));

        // A height map archived together with its JSON.
        m_fake.addPlugin(L"JSONMod.esp");
        TestUtil::BSABuilder archive;
        archive.addFile("textures\\json\\packed_p.dds", "DDS");
        archive.addFile("textures\\json\\packed_p.json", disabledJSON);
        archive.write(m_fake.dataDir() / L"JSONMod.bsa");

        m_game.emplace(m_fake.game());
        m_pgd = std::make_unique<PGDirectory>(&*m_game, m_fake.gameDir() / L"out");
        m_pgd->populateFileMap(true, false);
    }

    void TearDown() override
    {
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
        ExceptionHandler::reset();
    }

    /// @brief Maps the files on this thread, capturing every warning and error the mapping logs (see wasCaptured()).
    void mapAll(const std::vector<std::pair<std::wstring,
                                            TextureType>>& textureRules = { })
    {
        m_logStart = TestLog::entries().size();
        const ThreadCapture capture;
        m_pgd->mapFiles({ }, { }, textureRules, { }, false);
        m_pgd->waitForMeshMapping();
        m_pgd->waitForCMClassification();
    }

    /// @brief Whether the last mapAll() logged a message of the given level containing needle. Only levels below
    /// warnings, which Logger never suppresses as repeats.
    [[nodiscard]] bool wasLogged(spdlog::level::level_enum level,
                                 const std::string& needle) const
    {
        const auto entries = TestLog::entries();
        const auto start = static_cast<std::ptrdiff_t>(std::min(m_logStart, entries.size()));
        return std::ranges::any_of(entries | std::views::drop(start), [&level, &needle](const TestLog::Entry& entry) {
            return entry.level == level && entry.message.find(needle) != std::string::npos;
        });
    }

    [[nodiscard]] bool isIgnoredByItsJSON(const std::wstring& texture) const
    {
        return wasLogged(spdlog::level::debug,
                         "Ignoring texture disabled by its JSON: " + TestUtil::pathToUTF8(texture));
    }
};

TEST_F(PGDirectoryTextureJSONTest,
       AHeightMapDisabledByItsJSONIsLeftOut)
{
    mapAll({
        { L"textures\\json\\ruled_p.dds", TextureType::Height },
        { L"textures\\json\\ruledbroken_p.dds", TextureType::Height },
    });

    // Left out as if a texture rule set it to unknown, whatever the case of the file name and archived or not.
    for (const auto* base : { L"textures\\json\\off", L"textures\\json\\upper", L"textures\\json\\packed" }) {
        const auto texture = std::wstring(base) + L"_p.dds";
        EXPECT_EQ(m_pgd->textureType(texture), TextureType::Unknown) << TestUtil::pathToUTF8(texture);
        EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::Parallax).contains(base)) << TestUtil::pathToUTF8(texture);
        EXPECT_TRUE(isIgnoredByItsJSON(texture)) << TestUtil::pathToUTF8(texture);
    }

    // Everything else is mapped as usual: false, no key, the key in another case, a key that is not a boolean, a JSON
    // that is not an object or cannot be parsed, and no JSON at all.
    for (const auto* base : {
             L"textures\\json\\on",
             L"textures\\json\\nokey",
             L"textures\\json\\keycase",
             L"textures\\json\\string",
             L"textures\\json\\array",
             L"textures\\json\\broken",
             L"textures\\json\\plain",
         }) {
        const auto texture = std::wstring(base) + L"_p.dds";
        EXPECT_TRUE(mapHas(*m_pgd, TextureSlots::Parallax, base, texture, TextureType::Height))
            << TestUtil::pathToUTF8(texture);
    }
    EXPECT_TRUE(wasCaptured(spdlog::level::err, L"Failed to parse JSON: textures\\json\\broken_p.json"));

    // A texture rule takes precedence, and the JSON of a texture that a rule gives its type is not even read.
    EXPECT_TRUE(mapHas(
        *m_pgd, TextureSlots::Parallax, L"textures\\json\\ruled", L"textures\\json\\ruled_p.dds", TextureType::Height));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::Parallax,
                       L"textures\\json\\ruledbroken",
                       L"textures\\json\\ruledbroken_p.dds",
                       TextureType::Height));
    EXPECT_FALSE(wasCaptured(spdlog::level::err, L"textures\\json\\ruledbroken_p.json"));

    // One line counts every texture left out, environment masks included: off_p, upper_p, packed_p, voted, off_m,
    // cmoff_m, ruledcm_m and envvote.
    EXPECT_TRUE(wasLogged(spdlog::level::info, "Ignored 8 textures disabled by their JSON"));
}

TEST_F(PGDirectoryTextureJSONTest,
       TheVotedTypeDecidesWhetherTheJSONCounts)
{
    mapAll();

    // A parallax shape makes a height map and an environment map shape an environment mask of a texture without the
    // suffix, so their JSONs leave them out.
    EXPECT_TRUE(isIgnoredByItsJSON(L"textures\\json\\voted.dds"));
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\voted.dds"), TextureType::Unknown);
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::Parallax).contains(L"textures\\json\\voted"));
    EXPECT_TRUE(isIgnoredByItsJSON(L"textures\\json\\envvote.dds"));
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\envvote.dds"), TextureType::Unknown);
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(L"textures\\json\\envvote"));

    // A TruePBR shape and a diffuse slot make other types of _p and _m textures, whose JSONs do nothing.
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::Parallax,
                       L"textures\\json\\pbrvote",
                       L"textures\\json\\pbrvote_p.dds",
                       TextureType::HeightPBR));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::EnvMask,
                       L"textures\\json\\rmaosvote",
                       L"textures\\json\\rmaosvote_m.dds",
                       TextureType::RMAOS));
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\diffvote_p.dds"), TextureType::Diffuse);

    // Neither do the JSONs of the textures the suffix gives another type, the height maps of the TruePBR folder
    // included.
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\wall_n.dds"), TextureType::Normal);
    EXPECT_EQ(m_pgd->textureType(L"textures\\pbr\\json\\x_p.dds"), TextureType::HeightPBR);
    for (const auto* texture : {
             L"textures\\json\\pbrvote_p.dds",
             L"textures\\json\\rmaosvote_m.dds",
             L"textures\\json\\diffvote_p.dds",
             L"textures\\json\\wall_n.dds",
             L"textures\\pbr\\json\\x_p.dds",
         }) {
        EXPECT_FALSE(isIgnoredByItsJSON(texture)) << TestUtil::pathToUTF8(texture);
    }
}

TEST_F(PGDirectoryTextureJSONTest,
       AnEnvironmentMaskDisabledByItsJSONIsLeftOutBeforeItsCachedClassification)
{
    // PGLite's classification cache and the update cache answer for the masks they classified before. No PGD3D is
    // installed, so only a texture rule or an answer from the cache can map a mask here.
    CachedClassificationObserver observer;
    const ScopedObserver scopedObserver(observer);
    mapAll({ { L"textures\\json\\ruledcm_m.dds", TextureType::ComplexMaterial } });

    // The cache was asked for the one mask that neither a rule nor its JSON decides, and its answer mapped that mask.
    EXPECT_EQ(observer.questions, (std::vector<std::filesystem::path> { L"textures\\json\\cm_m.dds" }));
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::EnvMask,
                       L"textures\\json\\cm",
                       L"textures\\json\\cm_m.dds",
                       TextureType::ComplexMaterial));

    for (const auto* base : { L"textures\\json\\off", L"textures\\json\\cmoff" }) {
        const auto texture = std::wstring(base) + L"_m.dds";
        EXPECT_EQ(m_pgd->textureType(texture), TextureType::Unknown) << TestUtil::pathToUTF8(texture);
        EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(base)) << TestUtil::pathToUTF8(texture);
        EXPECT_TRUE(isIgnoredByItsJSON(texture)) << TestUtil::pathToUTF8(texture);
    }

    // A texture rule takes precedence here too: the mask it makes a complex material texture is mapped as one without a
    // classification, its JSON notwithstanding.
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::EnvMask,
                       L"textures\\json\\ruledcm",
                       L"textures\\json\\ruledcm_m.dds",
                       TextureType::ComplexMaterial));
    EXPECT_FALSE(isIgnoredByItsJSON(L"textures\\json\\ruledcm_m.dds"));
}

TEST_F(PGDirectoryTextureJSONTest,
       AComplexMaterialTextureDisabledByItsJSONIsNotMapped)
{
    if (!m_d3d.initGPU() || !m_d3d.initShaders())
        GTEST_SKIP() << "no Direct3D 11 hardware device";

    PGGlobals::setPGD(m_pgd.get());
    PGGlobals::setPGD3D(&m_d3d);
    mapAll();

    // The two textures are equal, only the JSON tells them apart.
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\cm_m.dds"), TextureType::ComplexMaterial);
    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::EnvMask,
                       L"textures\\json\\cm",
                       L"textures\\json\\cm_m.dds",
                       TextureType::ComplexMaterial));
    EXPECT_EQ(m_pgd->textureType(L"textures\\json\\cmoff_m.dds"), TextureType::Unknown);
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(L"textures\\json\\cmoff"));
}

TEST_F(PGDirectoryTextureJSONTest,
       AJSONThatCannotBeReadDisablesNothing)
{
    // A JSON that is gone by the time the textures are classified reads as empty, which is reported like a broken file.
    std::filesystem::remove(m_fake.dataDir() / L"textures\\json\\off_p.json");
    mapAll();

    EXPECT_TRUE(mapHas(
        *m_pgd, TextureSlots::Parallax, L"textures\\json\\off", L"textures\\json\\off_p.dds", TextureType::Height));
    EXPECT_TRUE(wasCaptured(spdlog::level::err, L"Failed to parse JSON: textures\\json\\off_p.json"));
}

TEST_F(PGDirectoryTextureJSONTest,
       AJSONWhoseReaderThrowsDisablesNothing)
{
    // No program classifies through a reader (PGLite installs its engine reader after the classification), but a
    // reader that throws must not end the classification either.
    m_pgd->setFileReader([](const std::filesystem::path& relPath) -> std::vector<std::byte> {
        throw BethesdaDirectory::ReadFailure("Unable to read " + TestUtil::pathToUTF8(relPath));
    });
    mapAll();

    for (const auto* base : { L"textures\\json\\off", L"textures\\json\\packed" }) {
        const auto texture = std::wstring(base) + L"_p.dds";
        EXPECT_TRUE(mapHas(*m_pgd, TextureSlots::Parallax, base, texture, TextureType::Height))
            << TestUtil::pathToUTF8(texture);
        EXPECT_TRUE(wasCaptured(spdlog::level::err, L"Unable to read JSON: " + std::wstring(base) + L"_p.json"))
            << TestUtil::pathToUTF8(texture);
    }
}

TEST_F(PGDirectoryTextureJSONTest,
       TheJSONThatWinsAtThePathCounts)
{
    // A loose JSON wins over the archived one at its path, so it enables the archived height map again.
    m_fake.writeDataFile(L"textures\\json\\packed_p.json", R"({"disabled": false})");
    m_pgd = std::make_unique<PGDirectory>(&*m_game, m_fake.gameDir() / L"out");
    m_pgd->populateFileMap(true, false);
    mapAll();

    EXPECT_TRUE(mapHas(*m_pgd,
                       TextureSlots::Parallax,
                       L"textures\\json\\packed",
                       L"textures\\json\\packed_p.dds",
                       TextureType::Height));
    EXPECT_FALSE(isIgnoredByItsJSON(L"textures\\json\\packed_p.dds"));
}

}
