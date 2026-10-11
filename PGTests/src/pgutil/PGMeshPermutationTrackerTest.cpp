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
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <boost/crc.hpp>
#include <spdlog/common.h>

#include <cstddef>
#include <filesystem>
#include <memory>
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

}
