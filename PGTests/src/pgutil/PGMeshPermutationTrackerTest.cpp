#include "pgutil/PGMeshPermutationTracker.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <boost/crc.hpp>
#include <spdlog/common.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureSlots;
using TestUtil::NIFBuilder;
using FormKey = PGMeshPermutationTracker::FormKey;
using CommitResult = PGMeshPermutationTracker::CommitResult;

const FormKey s_useA { .modKey = L"ModA.esp", .formID = 0x800, .subMODL = "" };
const FormKey s_useB { .modKey = L"ModB.esp", .formID = 0x801, .subMODL = "" };
const FormKey s_useC { .modKey = L"ModB.esp", .formID = 0x801, .subMODL = "MOD2" };

PGTypes::TextureSet stoneTextures()
{
    PGTypes::TextureSet textures;
    textures[0] = L"textures\\a\\stone.dds";
    textures[1] = L"textures\\a\\stone_n.dds";
    return textures;
}

unsigned long long crc32Of(const std::filesystem::path& file)
{
    const auto bytes = FileUtil::fileBytes(file);
    boost::crc_32_type crc;
    crc.process_bytes(bytes.data(), bytes.size());
    return crc.checksum();
}

/**
 * @brief A data folder with a two shape mesh and a weighted armor pair, mapped by the global PGDirectory.
 */
class PGMeshPermutationTrackerTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";
    static constexpr const wchar_t* weightedPath0 = L"meshes\\armor\\cuirass_0.nif";
    static constexpr const wchar_t* weightedPath1 = L"meshes\\armor\\cuirass_1.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }
    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }

    void SetUp() override
    {
        NIFBuilder stone;
        stone.addShape("First", stoneTextures());
        stone.addShape("Second", stoneTextures());
        stone.save(dataDir() / meshPath);

        for (const auto* weighted : { weightedPath0, weightedPath1 }) {
            NIFBuilder armor;
            armor.addShape("Body", stoneTextures());
            armor.save(dataDir() / weighted);
        }

        m_pgd = std::make_unique<PGDirectory>(dataDir(), outDir());
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());

        // Weighted variant state is shared between trackers, so start every test clean.
        PGMeshPermutationTracker::validateWeightedVariants();
    }

    void TearDown() override
    {
        PGGlobals::fileSaver().waitForCompletion();
        PGMeshPermutationTracker::validateWeightedVariants();
        PGGlobals::setPGD(nullptr);
    }

    /// @brief Stages the mesh and gives the first shape another diffuse texture.
    static nifly::NifFile* stageWithTexture(PGMeshPermutationTracker& tracker,
                                            const std::string& diffuse,
                                            size_t shapeIndex = 0)
    {
        auto* staged = tracker.stageMesh();
        auto* shape = staged->GetShapes().at(shapeIndex);
        PGNIFUtil::setTextureSlot(staged, shape, TextureSlots::Diffuse, diffuse);
        return staged;
    }

    static void waitForFiles() { PGGlobals::fileSaver().waitForCompletion(); }
};

TEST_F(PGMeshPermutationTrackerTest,
       ConstructorNeedsAMappedMesh)
{
    EXPECT_NO_THROW(PGMeshPermutationTracker { meshPath });
    EXPECT_THROW(PGMeshPermutationTracker { L"meshes\\a\\missing.nif" }, std::runtime_error);

    PGGlobals::setPGD(nullptr);
    EXPECT_THROW(PGMeshPermutationTracker { meshPath }, std::runtime_error);
}

TEST_F(PGMeshPermutationTrackerTest,
       LoadReadsTheOriginalMesh)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();

    auto* staged = tracker.stageMesh();
    ASSERT_NE(staged, nullptr);
    const auto shapes = staged->GetShapes();
    ASSERT_EQ(shapes.size(), 2U);
    EXPECT_EQ(shapes[0]->name.get(), "First");
    EXPECT_EQ(shapes[1]->name.get(), "Second");

    // Staging again gives a fresh copy of the original.
    PGNIFUtil::setTextureSlot(staged, shapes[0], TextureSlots::Diffuse, std::string("textures\\changed.dds"));
    auto const* fresh = tracker.stageMesh();
    EXPECT_EQ(PGNIFUtil::textureSlot(fresh, fresh->GetShapes()[0], TextureSlots::Diffuse), "textures\\a\\stone.dds");

    // Loading from an already loaded NIF.
    PGMeshPermutationTracker other(meshPath);
    EXPECT_THROW(other.load(nullptr, 1), std::runtime_error);
    const auto loaded = std::make_shared<nifly::NifFile>(PGNIFUtil::loadNIFFromBytes(m_pgd->file(meshPath)));
    other.load(loaded, 1234);
    EXPECT_EQ(other.stageMesh()->GetShapes().size(), 2U);
}

TEST_F(PGMeshPermutationTrackerTest,
       CommitNeedsAStagedMesh)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    EXPECT_THROW(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), std::runtime_error);

    tracker.stageMesh();
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::UnchangedFromOriginal);
    // The staging area is cleared by a commit.
    EXPECT_THROW(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), std::runtime_error);
}

TEST_F(PGMeshPermutationTrackerTest,
       UnchangedMeshesAreNotWritten)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    tracker.stageMesh();
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::UnchangedFromOriginal);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    EXPECT_TRUE(results.empty());
    EXPECT_EQ(crcs.first, crc32Of(dataDir() / meshPath));
    EXPECT_EQ(crcs.second, 0U);
    EXPECT_FALSE(std::filesystem::exists(outDir() / meshPath));
}

TEST_F(PGMeshPermutationTrackerTest,
       ChangedMeshBecomesTheBaseOutput)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].meshPath, meshPath);
    ASSERT_EQ(results[0].altTexResults.size(), 1U);
    EXPECT_EQ(results[0].altTexResults[0].first, s_useA);
    EXPECT_TRUE(results[0].altTexResults[0].second.empty());
    EXPECT_EQ(results[0].idxCorrections, (std::unordered_map<int, int> { { 0, 0 }, { 1, 1 } }));
    EXPECT_EQ(results[0].inverseIdxCorrectionsPatching, (std::unordered_map<int, int> { { 0, 0 }, { 1, 1 } }));

    const auto output = outDir() / meshPath;
    ASSERT_TRUE(std::filesystem::exists(output));
    EXPECT_EQ(crcs.first, crc32Of(dataDir() / meshPath));
    EXPECT_EQ(crcs.second, crc32Of(output));
    EXPECT_NE(crcs.first, crcs.second);
    EXPECT_TRUE(m_pgd->isGenerated(meshPath));

    const auto saved = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(output));
    EXPECT_EQ(PGNIFUtil::textureSlot(&saved, saved.GetShapes()[0], TextureSlots::Diffuse), "textures\\a\\patched.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&saved, saved.GetShapes()[1], TextureSlots::Diffuse), "textures\\a\\stone.dds");
}

TEST_F(PGMeshPermutationTrackerTest,
       EqualResultsShareOneOutput)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);
    stageWithTexture(tracker, "Textures\\A\\PATCHED.dds"); // same texture in another case
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::MergedIntoOutput);

    // A form key is only committed once.
    stageWithTexture(tracker, "textures\\a\\other.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::AlreadyProcessed);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    ASSERT_EQ(results[0].altTexResults.size(), 2U);
    EXPECT_EQ(results[0].altTexResults[0].first, s_useA);
    EXPECT_EQ(results[0].altTexResults[1].first, s_useB);
}

TEST_F(PGMeshPermutationTrackerTest,
       DifferentResultsBecomeDuplicates)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\first.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);
    stageWithTexture(tracker, "textures\\a\\second.dds");
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::Added);
    stageWithTexture(tracker, "textures\\a\\third.dds");
    EXPECT_EQ(tracker.commitMesh(s_useC, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 3U);
    EXPECT_EQ(results[0].meshPath, meshPath);
    EXPECT_EQ(results[1].meshPath, L"meshes\\_pgpatcher_dups\\1\\a\\stone.nif");
    EXPECT_EQ(results[2].meshPath, L"meshes\\_pgpatcher_dups\\2\\a\\stone.nif");
    for (const auto& result : results) {
        EXPECT_TRUE(std::filesystem::exists(outDir() / result.meshPath)) << TestUtil::pathToUTF8(result.meshPath);
        EXPECT_TRUE(m_pgd->isGenerated(result.meshPath));
    }

    const auto second = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / results[1].meshPath));
    EXPECT_EQ(PGNIFUtil::textureSlot(&second, second.GetShapes()[0], TextureSlots::Diffuse), "textures\\a\\second.dds");
}

TEST_F(PGMeshPermutationTrackerTest,
       AnUnchangedUseKeepsTheOriginalSoDuplicatesStartAtOne)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    tracker.stageMesh();
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::UnchangedFromOriginal);
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].meshPath, L"meshes\\_pgpatcher_dups\\1\\a\\stone.nif");
    EXPECT_EQ(crcs.second, 0U); // the base mesh was not written
    EXPECT_FALSE(std::filesystem::exists(outDir() / meshPath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / results[0].meshPath));
}

TEST_F(PGMeshPermutationTrackerTest,
       IgnoreBaseMeshMovesEveryOutputToTheDuplicates)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    tracker.ignoreBaseMesh();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].meshPath, L"meshes\\_pgpatcher_dups\\1\\a\\stone.nif");
    EXPECT_EQ(crcs.second, 0U);
}

TEST_F(PGMeshPermutationTrackerTest,
       ShapesWithAlternateTexturesAreNotComparedByTextureSet)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\first.dds", 1);
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    // The second use gives shape 1 another texture, but shape 1 has an alternate texture in the plugin, so the
    // texture set of the mesh does not matter and the use shares the first output.
    PGTypes::TextureSet altTex;
    altTex[0] = L"textures\\a\\alternate.dds";
    stageWithTexture(tracker, "textures\\a\\second.dds", 1);
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { { 1, altTex } }, { 0 }), CommitResult::MergedIntoOutput);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    ASSERT_EQ(results[0].altTexResults.size(), 2U);
    EXPECT_EQ(results[0].altTexResults[1].first, s_useB);
    ASSERT_TRUE(results[0].altTexResults[1].second.contains(1));
    EXPECT_EQ(results[0].altTexResults[1].second.at(1)[0], L"textures\\a\\alternate.dds");
}

TEST_F(PGMeshPermutationTrackerTest,
       DeletedShapesAreReportedInTheIndexCorrections)
{
    NIFBuilder three;
    three.addShape("A", stoneTextures());
    three.addShape("B", stoneTextures());
    three.addShape("C", stoneTextures());
    three.save(dataDir() / L"meshes\\a\\three.nif");
    m_pgd->populateFileMap(false, false);

    PGMeshPermutationTracker tracker(L"meshes\\a\\three.nif");
    tracker.load();
    auto* staged = tracker.stageMesh();
    staged->DeleteShape(staged->GetShapes().at(1));
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1, 2 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].idxCorrections, (std::unordered_map<int, int> { { 0, 0 }, { 1, -1 }, { 2, 1 } }));
    EXPECT_EQ(results[0].inverseIdxCorrectionsPatching, (std::unordered_map<int, int> { { 0, 0 }, { 1, 2 } }));

    const auto saved = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / results[0].meshPath));
    ASSERT_EQ(saved.GetShapes().size(), 2U);
    EXPECT_EQ(saved.GetShapes()[0]->name.get(), "A");
    EXPECT_EQ(saved.GetShapes()[1]->name.get(), "C");
}

TEST_F(PGMeshPermutationTrackerTest,
       ShaderFieldsVertexColorsAndEffectClampModeAreCompared)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();

    // A shader field.
    auto const* staged = tracker.stageMesh();
    dynamic_cast<nifly::BSLightingShaderProperty*>(staged->GetShader(staged->GetShapes()[0]))->glossiness = 99;
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    // Vertex colors.
    staged = tracker.stageMesh();
    staged->GetShapes()[0]->SetVertexColors(true);
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::Added);

    // Effect shaders are compared by their clamp mode only.
    NIFBuilder effectBuilder;
    effectBuilder.addEffectShape("Glow", "textures\\fx.dds");
    effectBuilder.save(dataDir() / L"meshes\\a\\effect.nif");
    m_pgd->populateFileMap(false, false);
    PGMeshPermutationTracker effectTracker(L"meshes\\a\\effect.nif");
    effectTracker.load();
    staged = effectTracker.stageMesh();
    auto* effect = dynamic_cast<nifly::BSEffectShaderProperty*>(staged->GetShader(staged->GetShapes()[0]));
    ASSERT_NE(effect, nullptr);
    effect->falloffStartAngle = 0.5F;
    EXPECT_EQ(effectTracker.commitMesh(s_useA, false, { }, { 0 }), CommitResult::UnchangedFromOriginal);
    staged = effectTracker.stageMesh();
    effect = dynamic_cast<nifly::BSEffectShaderProperty*>(staged->GetShader(staged->GetShapes()[0]));
    effect->textureClampMode = 3;
    EXPECT_EQ(effectTracker.commitMesh(s_useB, false, { }, { 0 }), CommitResult::Added);
}

TEST_F(PGMeshPermutationTrackerTest,
       AlphaPropertiesAreCompared)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();

    // An added alpha property makes a different mesh, and the same one again merges into that output.
    auto* staged = tracker.stageMesh();
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(*staged, *staged->GetShapes()[0], std::nullopt, std::nullopt, true));
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);
    staged = tracker.stageMesh();
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(*staged, *staged->GetShapes()[0], std::nullopt, std::nullopt, true));
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::MergedIntoOutput);

    // Another threshold is a different mesh again.
    staged = tracker.stageMesh();
    EXPECT_TRUE(PGNIFUtil::setAlphaProperty(*staged, *staged->GetShapes()[0], std::nullopt, 64, true));
    EXPECT_EQ(tracker.commitMesh(s_useC, false, { }, { 0, 1 }), CommitResult::Added);
}

TEST_F(PGMeshPermutationTrackerTest,
       ParticleSystemsTakePartInTheComparison)
{
    NIFBuilder builder;
    builder.addParticleSystem("Particles");
    builder.addShape("Shape", stoneTextures());
    builder.save(dataDir() / L"meshes\\a\\particles.nif");
    m_pgd->populateFileMap(false, false);

    PGMeshPermutationTracker tracker(L"meshes\\a\\particles.nif");
    tracker.load();
    tracker.stageMesh();
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 1 }), CommitResult::UnchangedFromOriginal);

    // The shape is at 3D index 1 behind the particle system.
    auto* staged = tracker.stageMesh();
    PGNIFUtil::setTextureSlot(staged, staged->GetShapes()[0], TextureSlots::Diffuse, std::string("textures\\x.dds"));
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 1 }), CommitResult::Added);
    // The same change again, with the shape excluded from the texture set comparison, merges.
    staged = tracker.stageMesh();
    PGNIFUtil::setTextureSlot(staged, staged->GetShapes()[0], TextureSlots::Diffuse, std::string("textures\\y.dds"));
    EXPECT_EQ(tracker.commitMesh(s_useC, false, { }, { }), CommitResult::MergedIntoOutput);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].meshPath, L"meshes\\_pgpatcher_dups\\1\\a\\particles.nif");

    // Saving sorts the blocks, which puts the shape in front of the particle system. The corrections map the original
    // 3D indices to where the objects ended up in the saved file.
    const auto saved = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / results[0].meshPath), false);
    std::vector<nifly::NiObject*> tree;
    saved.GetTree(tree);
    std::unordered_map<std::string, int> savedIndices;
    int index3D = 0;
    for (auto* block : tree)
        if (dynamic_cast<nifly::NiShape*>(block) || dynamic_cast<nifly::NiParticleSystem*>(block))
            savedIndices[dynamic_cast<nifly::NiAVObject*>(block)->name.get()] = index3D++;
    ASSERT_EQ(savedIndices.size(), 2U);
    EXPECT_EQ(savedIndices.at("Shape"), 0);
    EXPECT_EQ(results[0].idxCorrections,
              (std::unordered_map<int, int> { { 0, savedIndices.at("Particles") }, { 1, savedIndices.at("Shape") } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       SaveRefusesToOverwriteAnExistingOutput)
{
    TestUtil::writeFile(outDir() / meshPath, "already here");

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    ASSERT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);
    EXPECT_THROW(tracker.saveMeshes(), std::runtime_error);
}

TEST_F(PGMeshPermutationTrackerTest,
       OriginalObjectBoundsCoverTheLoadedMesh)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    // Both shapes are unit quads at the origin.
    EXPECT_EQ(tracker.originalObjectBounds(), (PGTypes::ObjectBounds { .min = { 0, 0, 0 }, .max = { 1, 1, 0 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       ObjectBoundsAccountForTheTransformsOfEveryShape)
{
    NIFBuilder builder;
    auto* first = builder.addShape("First");
    first->transform.translation = nifly::Vector3(-2.7F, 5.5F, 3.9F);
    // A shape below a scaled and moved node, which has to be placed in model space too.
    auto* node = builder.addNode("Node");
    node->transform.translation = nifly::Vector3(10, 0, 0);
    node->transform.scale = 2;
    auto* second = builder.addShape("Second", PGTypes::TextureSet(), nifly::BSLSP_DEFAULT, node);
    second->transform.translation = nifly::Vector3(0, 0, -1.5F);

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);

    // First spans x -2.7 to -1.7, y 5.5 to 6.5, z 3.9. Second spans x 10 to 12, y 0 to 2, z -3. The bounds are
    // truncated toward zero, like the OBND the Creation Kit writes.
    EXPECT_EQ(tracker.originalObjectBounds(), (PGTypes::ObjectBounds { .min = { -2, 0, -3 }, .max = { 12, 6, 3 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       ObjectBoundsAreClampedToTheRangeOfAnInt16)
{
    NIFBuilder builder;
    auto* distant = builder.addShape("Distant");
    distant->transform.translation = nifly::Vector3(40000, -40000, 0);

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    EXPECT_EQ(tracker.originalObjectBounds(),
              (PGTypes::ObjectBounds { .min = { INT16_MAX, INT16_MIN, 0 }, .max = { INT16_MAX, INT16_MIN, 0 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       AMeshWithoutVerticesHasNoObjectBounds)
{
    NIFBuilder builder;
    builder.addNode("Empty");

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    EXPECT_EQ(tracker.originalObjectBounds(), std::nullopt);
}

TEST_F(PGMeshPermutationTrackerTest,
       SavedMeshesCarryTheirObjectBoundsWhenAsked)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    ASSERT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes(false, true);
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].objectBounds, (PGTypes::ObjectBounds { .min = { 0, 0, 0 }, .max = { 1, 1, 0 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       SavedMeshesHaveNoObjectBoundsByDefault)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    ASSERT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);

    const auto [results, crcs] = tracker.saveMeshes();
    waitForFiles();
    ASSERT_EQ(results.size(), 1U);
    EXPECT_EQ(results[0].objectBounds, std::nullopt);
}

TEST_F(PGMeshPermutationTrackerTest,
       ObjectBoundsOfAMeshWhoseBlocksListEachOtherAreStillComputed)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Loop");
    shape->transform.translation = nifly::Vector3(5.5F, -3.5F, 2.5F);
    // The root lists itself as a child, which a walk up through the parents would never leave.
    builder.root()->childRefs.AddBlockRef(builder.nif().GetBlockID(builder.root()));

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    EXPECT_EQ(tracker.originalObjectBounds(), (PGTypes::ObjectBounds { .min = { 5, -3, 2 }, .max = { 6, -2, 2 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       ObjectBoundsSkipPositionsThatAreNotNumbers)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Broken");
    shape->transform.translation = nifly::Vector3(5.5F, 5.5F, 5.5F);
    // The corner at the origin loses its x coordinate, which would otherwise seed the box with a NaN.
    shape->vertData.at(0).vert.x = std::numeric_limits<float>::quiet_NaN();

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    EXPECT_EQ(tracker.originalObjectBounds(), (PGTypes::ObjectBounds { .min = { 5, 5, 5 }, .max = { 6, 6, 5 } }));
}

TEST_F(PGMeshPermutationTrackerTest,
       ObjectBoundsIgnoreShapesThatAreNotBelowTheRoot)
{
    NIFBuilder builder;
    builder.addShape("Detached")->transform.translation = nifly::Vector3(5.5F, -3.5F, 2.5F);
    builder.root()->childRefs.Clear(); // the shape stays in the file, but nothing links to it

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    EXPECT_EQ(tracker.originalObjectBounds(), std::nullopt);
}

TEST_F(PGMeshPermutationTrackerTest,
       SavedMeshesKeepTheirBoundingSpheresByDefault)
{
    NIFBuilder builder;
    builder.addShape("Stale", stoneTextures())->SetBounds(nifly::BoundingSphere(nifly::Vector3(500, 500, 500), 1));

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    ASSERT_EQ(tracker.commitMesh(s_useA, false, { }, { 0 }), CommitResult::Added);
    tracker.saveMeshes();
    waitForFiles();

    const auto saved = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / meshPath));
    EXPECT_NEAR(saved.GetShapes().at(0)->GetBounds().center.x, 500, 1e-3F);
}

TEST_F(PGMeshPermutationTrackerTest,
       SavedMeshesGetTheirBoundingSpheresRecalculatedWhenAsked)
{
    NIFBuilder builder;
    builder.addShape("Stale", stoneTextures())->SetBounds(nifly::BoundingSphere(nifly::Vector3(500, 500, 500), 1));

    PGMeshPermutationTracker tracker(meshPath);
    tracker.load(std::make_shared<nifly::NifFile>(builder.nif()), 0);
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    ASSERT_EQ(tracker.commitMesh(s_useA, false, { }, { 0 }), CommitResult::Added);
    tracker.saveMeshes(true, false);
    waitForFiles();

    // The shape is a unit quad, so the sphere is centered on the quad and goes through its corners.
    const auto saved = PGNIFUtil::loadNIFFromBytes(FileUtil::fileBytes(outDir() / meshPath));
    const auto bounds = saved.GetShapes().at(0)->GetBounds();
    EXPECT_NEAR(bounds.center.x, 0.5F, 1e-3F);
    EXPECT_NEAR(bounds.center.y, 0.5F, 1e-3F);
    EXPECT_NEAR(bounds.center.z, 0, 1e-3F);
    EXPECT_NEAR(bounds.radius, std::sqrt(0.5F), 1e-3F);
}

TEST(PGMeshPermutationTrackerStaticTest,
     OtherWeightVariantSwapsTheSuffix)
{
    EXPECT_EQ(PGMeshPermutationTracker::otherWeightVariant(L"meshes\\armor\\cuirass_0.nif"),
              L"meshes\\armor\\cuirass_1.nif");
    EXPECT_EQ(PGMeshPermutationTracker::otherWeightVariant(L"meshes\\armor\\cuirass_1.nif"),
              L"meshes\\armor\\cuirass_0.nif");
    EXPECT_EQ(PGMeshPermutationTracker::otherWeightVariant(L"meshes\\armor\\cuirass.nif"),
              L"meshes\\armor\\cuirass.nif");
    EXPECT_EQ(PGMeshPermutationTracker::otherWeightVariant(L"meshes\\armor\\cuirass_2.nif"),
              L"meshes\\armor\\cuirass_2.nif");
    EXPECT_EQ(PGMeshPermutationTracker::otherWeightVariant(L""), L"");
}

TEST_F(PGMeshPermutationTrackerTest,
       MatchingWeightVariantsPassValidation)
{
    for (const auto* path : { weightedPath0, weightedPath1 }) {
        PGMeshPermutationTracker tracker(path);
        tracker.load();
        stageWithTexture(tracker, "textures\\armor\\patched.dds");
        EXPECT_EQ(tracker.commitMesh(s_useA, true, { }, { 0 }), CommitResult::Added);
    }

    EXPECT_TRUE(PGMeshPermutationTracker::validateWeightedVariants().empty());
}

TEST_F(PGMeshPermutationTrackerTest,
       DivergingWeightVariantsAreReported)
{
    const auto patch = [](const wchar_t* path, const std::string& texture) {
        PGMeshPermutationTracker tracker(path);
        tracker.load();
        stageWithTexture(tracker, texture);
        EXPECT_EQ(tracker.commitMesh(s_useA, true, { }, { 0 }), CommitResult::Added);
    };
    // The comparison is limited to the block types, so variants that differ only in their textures are fine.
    TestLog::clear();
    patch(weightedPath0, "textures\\armor\\zero.dds");
    patch(weightedPath1, "textures\\armor\\one.dds");
    EXPECT_FALSE(TestLog::contains(spdlog::level::err, "Weighted mesh variants"));
    EXPECT_TRUE(PGMeshPermutationTracker::validateWeightedVariants().empty());

    // A variant with other blocks than its counterpart is reported as soon as both were committed.
    patch(weightedPath0, "textures\\armor\\zero.dds");
    {
        PGMeshPermutationTracker tracker(weightedPath1);
        tracker.load();
        auto* staged = tracker.stageMesh();
        staged->DeleteShape(staged->GetShapes().at(0));
        EXPECT_EQ(tracker.commitMesh(s_useA, true, { }, { }), CommitResult::Added);
    }
    EXPECT_TRUE(TestLog::contains(spdlog::level::err,
                                  "Weighted mesh variants 'meshes\\armor\\cuirass_1.nif' and "
                                  "'meshes\\armor\\cuirass_0.nif' do not match."));
    EXPECT_TRUE(PGMeshPermutationTracker::validateWeightedVariants().empty());

    // A counterpart that was never patched as weighted is fine.
    patch(weightedPath0, "textures\\armor\\alone.dds");
    EXPECT_TRUE(PGMeshPermutationTracker::validateWeightedVariants().empty());

    // A counterpart that was patched as weighted but produced another number of outputs is an error.
    patch(weightedPath0, "textures\\armor\\zero.dds");
    {
        PGMeshPermutationTracker tracker(weightedPath1);
        tracker.load();
        stageWithTexture(tracker, "textures\\armor\\zero.dds");
        EXPECT_EQ(tracker.commitMesh(s_useA, true, { }, { 0 }), CommitResult::Added);
        stageWithTexture(tracker, "textures\\armor\\extra.dds");
        EXPECT_EQ(tracker.commitMesh(s_useB, true, { }, { 0 }), CommitResult::Added);
    }
    const auto errors = PGMeshPermutationTracker::validateWeightedVariants();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].first, weightedPath1);
    EXPECT_NE(errors[0].second.find(L"do not match"), std::wstring::npos);
    EXPECT_TRUE(PGMeshPermutationTracker::validateWeightedVariants().empty()); // the state was cleared
}

TEST_F(PGMeshPermutationTrackerTest,
       BytesConstructorNeedsNoFileMap)
{
    const auto bytes = FileUtil::fileBytes(dataDir() / meshPath);

    // No file map lookup is made.
    PGGlobals::setPGD(nullptr);
    PGMeshPermutationTracker tracker(meshPath, bytes);
    PGGlobals::setPGD(m_pgd.get());

    auto* staged = tracker.stageMesh();
    ASSERT_NE(staged, nullptr);
    EXPECT_EQ(staged->GetShapes().size(), 2U);

    EXPECT_THROW(PGMeshPermutationTracker(meshPath, std::vector<std::byte> { }), std::runtime_error);
}

TEST_F(PGMeshPermutationTrackerTest,
       SerializeMeshesReturnsTheOutputsWithoutWriting)
{
    PGMeshPermutationTracker tracker(meshPath, FileUtil::fileBytes(dataDir() / meshPath));
    stageWithTexture(tracker, "textures\\a\\patched.dds");
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }), CommitResult::Added);
    stageWithTexture(tracker, "textures\\a\\other.dds");
    EXPECT_EQ(tracker.commitMesh(s_useB, false, { }, { 0, 1 }), CommitResult::Added);

    const auto outputs = tracker.serializeMeshes();
    ASSERT_EQ(outputs.size(), 2U);
    EXPECT_EQ(outputs[0].result.meshPath, meshPath);
    EXPECT_EQ(outputs[1].result.meshPath, L"meshes\\_pgpatcher_dups\\1\\a\\stone.nif");
    EXPECT_EQ(outputs[0].result.idxCorrections, (std::unordered_map<int, int> { { 0, 0 }, { 1, 1 } }));
    ASSERT_EQ(outputs[0].result.altTexResults.size(), 1U);
    EXPECT_EQ(outputs[0].result.altTexResults[0].first, s_useA);
    EXPECT_FALSE(outputs[0].bytes.empty());
    EXPECT_NE(outputs[0].crc32, 0U);
    EXPECT_NE(outputs[0].crc32, outputs[1].crc32);
    EXPECT_EQ(outputs[0].blockKinds,
              (std::vector<PGMeshPermutationTracker::BlockKind> { PGMeshPermutationTracker::BlockKind::TriShape,
                                                                  PGMeshPermutationTracker::BlockKind::TriShape }));
    EXPECT_EQ(outputs[0].blockKinds, outputs[1].blockKinds);

    // Nothing reached the output folder or the file map.
    EXPECT_FALSE(std::filesystem::exists(outDir() / meshPath));
    EXPECT_FALSE(m_pgd->isGenerated(meshPath));

    // The bytes are the saved mesh.
    std::vector<std::byte> bytes(outputs[0].bytes.size());
    std::ranges::transform(outputs[0].bytes, bytes.begin(), [](char ch) { return static_cast<std::byte>(ch); });
    const auto saved = PGNIFUtil::loadNIFFromBytes(bytes);
    EXPECT_EQ(PGNIFUtil::textureSlot(&saved, saved.GetShapes()[0], TextureSlots::Diffuse), "textures\\a\\patched.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&saved, saved.GetShapes()[1], TextureSlots::Diffuse), "textures\\a\\stone.dds");
}

TEST_F(PGMeshPermutationTrackerTest,
       BlockTypeSignatureFollowsThe3DIndexOrder)
{
    NIFBuilder withParticles;
    withParticles.addShape("Shape", stoneTextures());
    withParticles.addParticleSystem("Particles");
    withParticles.addNiTriShape("Old", stoneTextures());
    EXPECT_EQ(PGMeshPermutationTracker::blockTypeSignature(withParticles.nif()),
              (std::vector<PGMeshPermutationTracker::BlockKind> {
                  PGMeshPermutationTracker::BlockKind::TriShape,
                  PGMeshPermutationTracker::BlockKind::ParticleSystem,
                  PGMeshPermutationTracker::BlockKind::Shape,
              }));

    // The textures of the shapes do not matter, only the kinds and their order.
    NIFBuilder sameLayout;
    sameLayout.addShape("Other", PGTypes::TextureSet());
    sameLayout.addParticleSystem("P");
    sameLayout.addNiTriShape("Older", PGTypes::TextureSet());
    EXPECT_EQ(PGMeshPermutationTracker::blockTypeSignature(withParticles.nif()),
              PGMeshPermutationTracker::blockTypeSignature(sameLayout.nif()));

    NIFBuilder shapesOnly;
    shapesOnly.addShape("Shape", stoneTextures());
    shapesOnly.addShape("Second", stoneTextures());
    EXPECT_NE(PGMeshPermutationTracker::blockTypeSignature(withParticles.nif()),
              PGMeshPermutationTracker::blockTypeSignature(shapesOnly.nif()));
    EXPECT_TRUE(PGMeshPermutationTracker::blockTypeSignature(NIFBuilder().nif()).empty());

    // The signature decides exactly what the weighted layout check of compareMesh decides.
    EXPECT_TRUE(PGMeshPermutationTracker::compareMesh(withParticles.nif(), sameLayout.nif(), { }, true, true));
    EXPECT_FALSE(PGMeshPermutationTracker::compareMesh(withParticles.nif(), shapesOnly.nif(), { }, true, true));
}

TEST_F(PGMeshPermutationTrackerTest,
       ACommitWithoutTheOriginalCheckKeepsAnUnchangedMeshAsAnOutput)
{
    PGMeshPermutationTracker tracker(meshPath);
    tracker.load();
    tracker.stageMesh();
    EXPECT_EQ(tracker.commitMesh(s_useA, false, { }, { 0, 1 }, false), CommitResult::Added);

    const auto serialized = tracker.serializeMeshes();
    ASSERT_EQ(serialized.size(), 1U);
    EXPECT_FALSE(serialized[0].bytes.empty());
    EXPECT_EQ(serialized[0].result.meshPath, meshPath); // the base output, since the base mesh is not ignored
}

}
