#include "PGPatcher.hpp"

#include "DDSBuilder.hpp"
#include "NIFBuilder.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGModManager.hpp"
#include "PGPlugin.hpp"
#include "PGRunCache.hpp"
#include "TestUtil.hpp"
#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"
#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"
#include "patchers/PatcherMeshPreStockMarker.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderVanillaParallax.hpp"
#include "patchers/base/PatcherUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::ShapeShader;
using PGEnums::TextureSlots;
using PGEnums::TextureType;
using FormKey = PGMeshPermutationTracker::FormKey;
using TestUtil::NIFBuilder;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    return set;
}

std::shared_ptr<PGModManager::Mod> makeMod(const std::wstring& name,
                                           int priority,
                                           bool isEnabled = true)
{
    auto mod = std::make_shared<PGModManager::Mod>();
    mod->name = name;
    mod->priority = priority;
    mod->isEnabled = isEnabled;
    return mod;
}

PatcherUtil::ShaderPatcherMatch shaderMatch(const std::shared_ptr<PGModManager::Mod>& mod,
                                            ShapeShader shader,
                                            const std::wstring& path,
                                            ShapeShader transformTo = ShapeShader::Unknown)
{
    PatcherUtil::ShaderPatcherMatch match;
    match.mod = mod;
    match.shader = shader;
    match.match.matchedPath = path;
    match.shaderTransformTo = transformTo;
    return match;
}

/**
 * @brief A data folder with meshes for every outcome of a patch run (complex material, parallax, nothing to do, a
 * facegen mesh) and their textures, mapped by the global PGDirectory, with a global PGD3D for the aspect ratio checks.
 */
class PGPatcherTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d;
    uint32_t m_behindBlockID = 0; /** Block ID of the second shape of the marker mesh as it was built */
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* stonePath = L"meshes\\pgtest\\stone.nif";
    static constexpr const wchar_t* woodPath = L"meshes\\pgtest\\wood.nif";
    static constexpr const wchar_t* plainPath = L"meshes\\pgtest\\plain.nif";
    static constexpr const wchar_t* sharedPath = L"meshes\\pgtest\\shared.nif";
    static constexpr const wchar_t* markerPath = L"meshes\\pgtest\\marker.nif";
    static constexpr const wchar_t* facegenPath
        = L"meshes\\actors\\character\\facegendata\\facegeom\\pgtest.esp\\00000800.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }
    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }

    void SetUp() override
    {
        for (const auto* texture : {
                 L"textures\\pgtest\\stone.dds",
                 L"textures\\pgtest\\stone_n.dds",
                 L"textures\\pgtest\\stone_p.dds",
                 L"textures\\pgtest\\stone_m.dds",
                 L"textures\\pgtest\\wood.dds",
                 L"textures\\pgtest\\wood_n.dds",
                 L"textures\\pgtest\\wood_p.dds",
                 L"textures\\pgtest\\plain.dds",
                 L"textures\\pgtest\\plain_n.dds",
             }) {
            TestUtil::DDSBuilder::writeDDS(dataDir() / texture, 16, 16);
        }

        const auto writeMesh = [this](const wchar_t* path, const std::wstring& base) {
            NIFBuilder builder;
            builder.addShape("Shape",
                             slots(L"textures\\pgtest\\" + base + L".dds", L"textures\\pgtest\\" + base + L"_n.dds"));
            builder.save(dataDir() / path);
        };
        writeMesh(stonePath, L"stone");
        writeMesh(woodPath, L"wood");
        writeMesh(plainPath, L"plain");
        writeMesh(facegenPath, L"stone");

        // Two shapes on one shader and one texture set, the second one excluded from shader patching.
        NIFBuilder shared;
        auto* patched
            = shared.addShape("Patched", slots(L"textures\\pgtest\\stone.dds", L"textures\\pgtest\\stone_n.dds"));
        auto* ignored = shared.addShaderlessShape("Ignored");
        ignored->ShaderPropertyRef()->index = patched->ShaderPropertyRef()->index;
        shared.addBooleanExtraData(ignored, "PG_IGNORE", true);
        shared.save(dataDir() / sharedPath);

        // A shape whose PG_STOCK block removes its alpha property, with a second shape behind it in the block list.
        NIFBuilder marked;
        auto* alphaShape
            = marked.addShape("Alpha", slots(L"textures\\pgtest\\plain.dds", L"textures\\pgtest\\plain_n.dds"));
        marked.addAlphaProperty(alphaShape);
        marked.addStringExtraData(alphaShape, PatcherMeshPreStockMarker::extraDataName, R"({"alpha_property": null})");
        auto* behind
            = marked.addShape("Behind", slots(L"textures\\pgtest\\plain.dds", L"textures\\pgtest\\plain_n.dds"));
        m_behindBlockID = marked.nif().GetBlockID(behind);
        marked.save(dataDir() / markerPath);

        std::filesystem::create_directories(outDir());
        TestUtil::writeFile(outDir() / L"meta.ini", "[General]");

        m_pgd = std::make_unique<PGDirectory>(dataDir(), outDir());
        m_pgd->populateFileMap(false, false);
        m_pgd->mapFiles({ }, { }, { { L"textures\\pgtest\\stone_m.dds", TextureType::ComplexMaterial } }, { }, false);
        m_pgd->waitForMeshMapping();
        PGGlobals::setPGD(m_pgd.get());
        PGGlobals::setPGD3D(&m_d3d);
        PGRunCache::initialize(outDir() / PGRunCache::s_cacheFilename, false);
        PGPatcher::resetRunState();

        PatcherUtil::PatcherMeshSet meshPatchers;
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreStockMarker::factory());
        meshPatchers.prePatchers.emplace_back(PatcherMeshPreFixTextureSlotCount::factory());
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderDefault::shaderType(),
                                            PatcherMeshShaderDefault::factory());
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderVanillaParallax::shaderType(),
                                            PatcherMeshShaderVanillaParallax::factory());
        meshPatchers.shaderPatchers.emplace(PatcherMeshShaderComplexMaterial::shaderType(),
                                            PatcherMeshShaderComplexMaterial::factory());
        meshPatchers.postPatchers.emplace_back(PatcherMeshPostRestoreDefaultShaders::factory());
        PGPatcher::loadPatchers(meshPatchers, { });
    }

    void TearDown() override
    {
        PGGlobals::fileSaver().waitForCompletion();
        PGPatcher::resetRunState();
        PGPatcher::loadPatchers(PatcherUtil::PatcherMeshSet(), PatcherUtil::PatcherTextureSet());
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
    }

    [[nodiscard]] nifly::NifFile loadOutput(const std::filesystem::path& relPath) const
    {
        return PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / relPath), false);
    }
};

TEST_F(PGPatcherTest,
       PatchMeshesWritesThePatchedMeshesAndTheirMetadata)
{
    EXPECT_TRUE(PGPatcher::isOutputEmpty()); // meta.ini does not count
    EXPECT_FALSE(PGPatcher::hasConflictData());

    std::vector<std::pair<size_t, size_t>> progress;
    std::vector<PGPatcher::MeshPatchStage> stages;
    PGPatcher::patchMeshes(
        false,
        true,
        { },
        false,
        false,
        [&progress](size_t done, size_t total) { progress.emplace_back(done, total); },
        [&stages](PGPatcher::MeshPatchStage stage) { stages.push_back(stage); });
    PGGlobals::fileSaver().waitForCompletion();

    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back(), std::make_pair(size_t { 6 }, size_t { 6 }));
    EXPECT_EQ(stages, (std::vector<PGPatcher::MeshPatchStage> { PGPatcher::MeshPatchStage::Patching }));
    EXPECT_FALSE(PGPatcher::isOutputEmpty());
    EXPECT_TRUE(PGPatcher::hasConflictData());

    // Complex material wins over parallax for the stone.
    ASSERT_TRUE(std::filesystem::exists(outDir() / stonePath));
    const auto stone = loadOutput(stonePath);
    auto* stoneShader = dynamic_cast<nifly::BSLightingShaderProperty*>(stone.GetShader(stone.GetShapes().at(0)));
    ASSERT_NE(stoneShader, nullptr);
    EXPECT_EQ(stoneShader->GetShaderType(), nifly::BSLSP_ENVMAP);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(stoneShader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    const auto stoneSlots = PGNIFUtil::textureSlots(&stone, stone.GetShapes().at(0));
    EXPECT_EQ(stoneSlots[5], L"textures\\pgtest\\stone_m.dds");
    EXPECT_EQ(stoneSlots[4], PatcherMeshShaderComplexMaterial::s_dynCubemapPath.wstring());
    EXPECT_TRUE(stoneSlots[3].empty());
    EXPECT_EQ(m_pgd->isGenerated(stonePath), true);

    // Only a height map exists for the wood.
    ASSERT_TRUE(std::filesystem::exists(outDir() / woodPath));
    const auto wood = loadOutput(woodPath);
    auto* woodShader = dynamic_cast<nifly::BSLightingShaderProperty*>(wood.GetShader(wood.GetShapes().at(0)));
    ASSERT_NE(woodShader, nullptr);
    EXPECT_EQ(woodShader->GetShaderType(), nifly::BSLSP_PARALLAX);
    EXPECT_EQ(PGNIFUtil::textureSlots(&wood, wood.GetShapes().at(0))[3], L"textures\\pgtest\\wood_p.dds");

    // Nothing applies to the plain mesh, so it is not written; the facegen mesh is patched like any other.
    EXPECT_FALSE(std::filesystem::exists(outDir() / plainPath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / facegenPath));

    // The diff lists the meshes that were written.
    const auto diff = PGPatcher::diffJSON();
    EXPECT_EQ(diff.size(), 5U) << diff.dump();
    ASSERT_TRUE(diff.contains("meshes\\pgtest\\stone.nif"));
    EXPECT_NE(diff["meshes\\pgtest\\stone.nif"]["crc32original"], diff["meshes\\pgtest\\stone.nif"]["crc32patched"]);
    EXPECT_FALSE(diff.contains("meshes\\pgtest\\plain.nif"));

    // The conflict viewer metadata lists every match of the dummy use.
    const auto meta = PGPatcher::patchMeta();
    ASSERT_TRUE(meta.contains(stonePath));
    const FormKey dummy { .modKey = L"", .formID = 0, .subMODL = "" };
    EXPECT_EQ(meta.at(stonePath).formKeys, (std::vector<FormKey> { dummy }));
    ASSERT_TRUE(meta.at(stonePath).shapeMeta.contains(0));
    const auto& shapeMeta = meta.at(stonePath).shapeMeta.at(0);
    EXPECT_EQ(shapeMeta.shapeName, "Shape");
    ASSERT_TRUE(shapeMeta.matches.contains(dummy));
    const auto& matches = shapeMeta.matches.at(dummy);
    ASSERT_EQ(matches.size(), 4U); // complex material, parallax and the default for the diffuse and the normal
    EXPECT_EQ(matches[0].shader, ShapeShader::ComplexMaterial);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\pgtest\\stone_m.dds");
    EXPECT_EQ(matches[1].shader, ShapeShader::VanillaParallax);
    EXPECT_EQ(matches[1].matchedPath, L"textures\\pgtest\\stone_p.dds");
    EXPECT_EQ(matches[2].shader, ShapeShader::None);
    EXPECT_EQ(matches[2].matchedPath, L"textures\\pgtest\\stone.dds");
    EXPECT_EQ(matches[3].shader, ShapeShader::None);
    EXPECT_EQ(matches[3].matchedPath, L"textures\\pgtest\\stone_n.dds");
    EXPECT_TRUE(shapeMeta.prePatchersApplied.empty());
    EXPECT_TRUE(shapeMeta.postPatchersApplied.empty());
    ASSERT_TRUE(meta.contains(plainPath));
    const auto& plainMatches = meta.at(plainPath).shapeMeta.at(0).matches.at(dummy);
    ASSERT_EQ(plainMatches.size(), 2U); // only the default, for the diffuse and the normal
    EXPECT_EQ(plainMatches[0].shader, ShapeShader::None);
    EXPECT_EQ(plainMatches[1].shader, ShapeShader::None);

    // Resetting forgets the run.
    PGPatcher::resetRunState();
    EXPECT_FALSE(PGPatcher::hasConflictData());
    EXPECT_TRUE(PGPatcher::patchMeta().empty());
    EXPECT_TRUE(PGPatcher::diffJSON().empty());

    // The texture phase has nothing to do without texture patchers or hooks.
    progress.clear();
    PGPatcher::patchTextures(false, [&progress](size_t done, size_t total) { progress.emplace_back(done, total); });
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back().first, progress.back().second);
    EXPECT_EQ(progress.back().second, 9U);
}

TEST_F(PGPatcherTest,
       ShapeBlockIDsAreTakenBeforeAnyBlockIsDeleted)
{
    PGPatcher::patchMeshes(false, true);
    PGGlobals::fileSaver().waitForCompletion();

    // The stock marker deleted the alpha property of the first shape, which renumbered the second shape.
    ASSERT_TRUE(std::filesystem::exists(outDir() / markerPath));
    const auto marked = loadOutput(markerPath);
    nifly::NiShape* alphaShape = nullptr;
    for (auto* const shape : marked.GetShapes())
        if (shape->name.get() == "Alpha")
            alphaShape = shape;
    ASSERT_NE(alphaShape, nullptr);
    EXPECT_EQ(marked.GetAlphaProperty(alphaShape), nullptr);

    // The metadata still names the second shape by the block ID it has in the mesh as shipped.
    const auto meta = PGPatcher::patchMeta();
    ASSERT_TRUE(meta.contains(markerPath));
    ASSERT_TRUE(meta.at(markerPath).shapeMeta.contains(1));
    EXPECT_EQ(meta.at(markerPath).shapeMeta.at(1).shapeName, "Behind");
    EXPECT_EQ(meta.at(markerPath).shapeMeta.at(1).blockID, m_behindBlockID);
}

TEST_F(PGPatcherTest,
       ShapesSharingAShaderArePatchedIndependently)
{
    PGPatcher::patchMeshes(false, true);
    PGGlobals::fileSaver().waitForCompletion();

    ASSERT_TRUE(std::filesystem::exists(outDir() / sharedPath));
    const auto shared = loadOutput(sharedPath);
    nifly::NiShape* patched = nullptr;
    nifly::NiShape* ignored = nullptr;
    for (auto* const shape : shared.GetShapes())
        if (shape->name.get() == "Patched")
            patched = shape;
        else if (shape->name.get() == "Ignored")
            ignored = shape;
    ASSERT_NE(patched, nullptr);
    ASSERT_NE(ignored, nullptr);

    // The patched shape got its own shader and texture set with the complex material result.
    auto* const patchedShader = dynamic_cast<nifly::BSLightingShaderProperty*>(shared.GetShader(patched));
    auto* const ignoredShader = dynamic_cast<nifly::BSLightingShaderProperty*>(shared.GetShader(ignored));
    ASSERT_NE(patchedShader, nullptr);
    ASSERT_NE(ignoredShader, nullptr);
    EXPECT_NE(patchedShader, ignoredShader);
    EXPECT_NE(patchedShader->textureSetRef.index, ignoredShader->textureSetRef.index);
    EXPECT_EQ(patchedShader->GetShaderType(), nifly::BSLSP_ENVMAP);
    EXPECT_TRUE(PGNIFUtil::hasShaderFlag(patchedShader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_EQ(PGNIFUtil::textureSlots(&shared, patched)[5], L"textures\\pgtest\\stone_m.dds");

    // The ignored shape keeps the shader and the textures the mesh shipped with.
    EXPECT_EQ(ignoredShader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(ignoredShader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_EQ(PGNIFUtil::textureSlots(&shared, ignored),
              slots(L"textures\\pgtest\\stone.dds", L"textures\\pgtest\\stone_n.dds"));
}

TEST_F(PGPatcherTest,
       PatchMeshesHonorsTheFacegenAndRecordTypeFilters)
{
    PGPatcher::patchMeshes(false, true, { }, false, true);
    PGGlobals::fileSaver().waitForCompletion();
    EXPECT_FALSE(std::filesystem::exists(outDir() / facegenPath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / stonePath));
    EXPECT_FALSE(PGPatcher::patchMeta().contains(facegenPath));

    // Without plugin uses and without forcing the base patch nothing is patched.
    std::filesystem::remove_all(outDir() / L"meshes");
    m_pgd->clearGeneratedFiles();
    PGPatcher::resetRunState();
    PGPatcher::patchMeshes(false, false);
    PGGlobals::fileSaver().waitForCompletion();
    EXPECT_FALSE(std::filesystem::exists(outDir() / stonePath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / facegenPath)); // facegen meshes never have plugin uses
    EXPECT_TRUE(PGPatcher::patchMeta().contains(facegenPath));

    // Dummy uses pass the record type filter.
    std::filesystem::remove_all(outDir() / L"meshes");
    m_pgd->clearGeneratedFiles();
    PGPatcher::resetRunState();
    PGPatcher::patchMeshes(false, true, { PGPlugin::ModelRecordType::Weapon }, true, false);
    PGGlobals::fileSaver().waitForCompletion();
    EXPECT_TRUE(std::filesystem::exists(outDir() / stonePath));
}

TEST_F(PGPatcherTest,
       OutputDirectoryCleanupKeepsForeignFilesSafe)
{
    PGPatcher::patchMeshes(false, true);
    PGGlobals::fileSaver().waitForCompletion();
    TestUtil::writeFile(outDir() / L"PGPatcher.esp", "plugin");
    TestUtil::writeFile(outDir() / L"PG_1.esp", "plugin");
    TestUtil::writeFile(outDir() / L"ParallaxGen_Diff.json", "{}");
    TestUtil::writeFile(outDir() / PGRunCache::s_cacheFilename, "cache");
    TestUtil::writeFile(outDir() / L"textures\\pgtest\\stone_m.dds", "generated");
    TestUtil::writeFile(outDir() / L"pgpatcher_output.zip", "zip");

    // A file PGPatcher did not create stops the cleanup.
    TestUtil::writeFile(outDir() / L"readme.txt", "mine");
    PGPatcher::deleteOutputDir(true, false);
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, "Output directory has non-PGPatcher related files"));
    EXPECT_TRUE(std::filesystem::exists(outDir() / stonePath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"PG_1.esp"));
    std::filesystem::remove(outDir() / L"readme.txt");

    // Keeping the incremental output keeps meshes, textures and the cache, and deletes the plugins and the diff.
    PGPatcher::deleteOutputDir(false, true);
    EXPECT_TRUE(std::filesystem::exists(outDir() / stonePath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"textures\\pgtest\\stone_m.dds"));
    EXPECT_TRUE(std::filesystem::exists(outDir() / PGRunCache::s_cacheFilename));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"pgpatcher_output.zip")); // not a pre-output cleanup
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"PGPatcher.esp"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"PG_1.esp"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"ParallaxGen_Diff.json"));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"meta.ini"));
    EXPECT_FALSE(PGPatcher::isOutputEmpty());

    // A full cleanup before a run leaves only the mod manager metadata.
    PGPatcher::deleteOutputDir(true, false);
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"meshes"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"textures"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / PGRunCache::s_cacheFilename));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"pgpatcher_output.zip"));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"meta.ini"));
    EXPECT_TRUE(PGPatcher::isOutputEmpty());

    // A missing output directory is empty and needs no cleanup.
    std::filesystem::remove_all(outDir());
    EXPECT_TRUE(PGPatcher::isOutputEmpty());
    EXPECT_NO_THROW(PGPatcher::deleteOutputDir(true, false));
}

TEST(PGPatcherSortTest,
     ShaderMatchesAreSortedByModPriorityShaderAndPath)
{
    const auto high = makeMod(L"High", 10);
    const auto low = makeMod(L"Low", 1);
    std::vector<PatcherUtil::ShaderPatcherMatch> matches {
        shaderMatch(nullptr, ShapeShader::TruePBR, L"textures\\z.dds"),
        shaderMatch(low, ShapeShader::ComplexMaterial, L"textures\\b.dds"),
        shaderMatch(high, ShapeShader::VanillaParallax, L"textures\\c.dds"),
        shaderMatch(high, ShapeShader::VanillaParallax, L"textures\\a.dds"),
        shaderMatch(high, ShapeShader::VanillaParallax, L"textures\\d.dds", ShapeShader::ComplexMaterial),
        shaderMatch(nullptr, ShapeShader::None, L"textures\\y.dds"),
    };
    PGPatcher::sortMatches(matches);

    // Matches with a mod come first, higher priority first; then the better (transformed) shader, then the path.
    ASSERT_EQ(matches.size(), 6U);
    EXPECT_EQ(matches[0].match.matchedPath, L"textures\\d.dds"); // high, upgraded to complex material
    EXPECT_EQ(matches[1].match.matchedPath, L"textures\\a.dds");
    EXPECT_EQ(matches[2].match.matchedPath, L"textures\\c.dds");
    EXPECT_EQ(matches[3].match.matchedPath, L"textures\\b.dds"); // low priority mod
    EXPECT_EQ(matches[4].match.matchedPath, L"textures\\z.dds"); // no mod, PBR
    EXPECT_EQ(matches[5].match.matchedPath, L"textures\\y.dds"); // no mod, default
}

TEST(PGPatcherSortTest,
     MatchMetadataIsSortedByTheGivenModOrder)
{
    const auto first = makeMod(L"First", 0);
    const auto second = makeMod(L"Second", 0);
    const auto unknown = makeMod(L"Unknown", 0);
    const auto meta = [](const std::shared_ptr<PGModManager::Mod>& mod,
                         ShapeShader shader,
                         const wchar_t* path,
                         ShapeShader transformTo = ShapeShader::Unknown) {
        PGPatcher::MatchMeta match;
        match.mod = mod;
        match.shader = shader;
        match.matchedPath = path;
        match.shaderTransformTo = transformTo;
        return match;
    };
    std::vector<PGPatcher::MatchMeta> matches {
        meta(nullptr, ShapeShader::TruePBR, L"textures\\none.dds"),
        meta(unknown, ShapeShader::TruePBR, L"textures\\unknown.dds"),
        meta(second, ShapeShader::ComplexMaterial, L"textures\\second.dds"),
        meta(first, ShapeShader::VanillaParallax, L"textures\\first_b.dds"),
        meta(first, ShapeShader::VanillaParallax, L"textures\\first_a.dds"),
        meta(first, ShapeShader::VanillaParallax, L"textures\\first_c.dds", ShapeShader::ComplexMaterial),
    };
    PGPatcher::sortMatches(matches, { first, second });

    ASSERT_EQ(matches.size(), 6U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\first_c.dds");
    EXPECT_EQ(matches[1].matchedPath, L"textures\\first_a.dds");
    EXPECT_EQ(matches[2].matchedPath, L"textures\\first_b.dds");
    EXPECT_EQ(matches[3].matchedPath, L"textures\\second.dds");
    // Mods that are not in the list and no mod at all rank last, in path order.
    EXPECT_EQ(matches[4].matchedPath, L"textures\\none.dds");
    EXPECT_EQ(matches[5].matchedPath, L"textures\\unknown.dds");
}

TEST_F(PGPatcherTest,
       TheMatchesDigestFollowsTheSlotsAndThePatchers)
{
    const auto stone = slots(L"textures\\pgtest\\stone.dds", L"textures\\pgtest\\stone_n.dds");
    const auto digest
        = PGPatcher::computeMatchesDigest(stonePath, stone, false, PGPlugin::ModelRecordType::StaticObject);
    EXPECT_EQ(PGPatcher::computeMatchesDigest(stonePath, stone, false, PGPlugin::ModelRecordType::StaticObject),
              digest);
    EXPECT_NE(PGPatcher::computeMatchesDigest(
                  stonePath, slots(L"textures\\pgtest\\wood.dds"), false, PGPlugin::ModelRecordType::StaticObject),
              digest);
    EXPECT_NE(PGPatcher::computeMatchesDigest(
                  stonePath, slots(L"textures\\pgtest\\plain.dds"), false, PGPlugin::ModelRecordType::StaticObject),
              digest);

    // Fewer patchers, fewer matches, another digest.
    PatcherUtil::PatcherMeshSet onlyDefault;
    onlyDefault.shaderPatchers.emplace(PatcherMeshShaderDefault::shaderType(), PatcherMeshShaderDefault::factory());
    PGPatcher::loadPatchers(onlyDefault, { });
    EXPECT_NE(PGPatcher::computeMatchesDigest(stonePath, stone, false, PGPlugin::ModelRecordType::StaticObject),
              digest);
}

TEST(PGPatcherUseFilterTest,
     IsUseUnpatchedFollowsTheIgnoreFlagAndTheRecordTypeFilter)
{
    const std::unordered_set<PGPlugin::ModelRecordType> allowed { PGPlugin::ModelRecordType::StaticObject };
    PGPlugin::MeshUseAttributes use;
    use.recType = PGPlugin::ModelRecordType::StaticObject;
    EXPECT_FALSE(PGPatcher::isUseUnpatched(use, allowed, true));
    EXPECT_FALSE(PGPatcher::isUseUnpatched(use, allowed, false));

    use.recType = PGPlugin::ModelRecordType::Weapon;
    EXPECT_TRUE(PGPatcher::isUseUnpatched(use, allowed, true));
    EXPECT_FALSE(PGPatcher::isUseUnpatched(use, allowed, false)); // the filter is off
    use.isDummyUse = true;
    EXPECT_FALSE(PGPatcher::isUseUnpatched(use, allowed, true)); // a dummy use has no record type to filter
    use.isDummyUse = false;

    use.recType = PGPlugin::ModelRecordType::StaticObject;
    use.isIgnored = true;
    EXPECT_TRUE(PGPatcher::isUseUnpatched(use, allowed, true));
    EXPECT_TRUE(PGPatcher::isUseUnpatched(use, allowed, false));
}

}
