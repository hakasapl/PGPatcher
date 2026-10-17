#include "PGRunCache.hpp"
#include "PGRunObserver.hpp"

#include "DDSBuilder.hpp"
#include "NIFBuilder.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPatcher.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaDirectory.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/Logger.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureAttribute;
using PGEnums::TextureSlots;
using PGEnums::TextureType;
using FormKey = PGMeshPermutationTracker::FormKey;
using HookKind = PGRunCache::HookKind;

const FormKey s_use { .modKey = L"ModA.esp", .formID = 0x800, .subMODL = "MODL" };

/**
 * @brief A data folder with a mesh and its textures, mapped by the global PGDirectory, with an empty output folder and
 * the run cache file inside it.
 */
class PGRunCacheTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }
    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }
    [[nodiscard]] std::filesystem::path cacheFile() const { return outDir() / PGRunCache::s_cacheFilename; }

    void SetUp() override
    {
        for (const auto* texture : {
                 L"textures\\a\\stone.dds",
                 L"textures\\a\\stone_n.dds",
                 L"textures\\a\\stone_p.dds",
                 L"textures\\a\\stone_m.dds",
             }) {
            TestUtil::DDSBuilder::writeDDS(dataDir() / texture, 16, 16);
        }
        TestUtil::NIFBuilder builder;
        PGTypes::TextureSet textures;
        textures[0] = L"textures\\a\\stone.dds";
        textures[1] = L"textures\\a\\stone_n.dds";
        builder.addShape("Stone", textures);
        builder.save(dataDir() / meshPath);
        std::filesystem::create_directories(outDir());

        m_pgd = std::make_unique<PGDirectory>(dataDir(), outDir());
        m_pgd->populateFileMap(false, false);
        m_pgd->mapFiles({ }, { }, { { L"textures\\a\\stone_m.dds", TextureType::ComplexMaterial } }, { }, false);
        m_pgd->waitForMeshMapping();
        m_pgd->addTextureAttribute(L"textures\\a\\stone_m.dds", TextureAttribute::CMMetalness);
        m_pgd->addTextureAttribute(L"textures\\a\\stone_m.dds", TextureAttribute::CMGlossiness);
        PGGlobals::setPGD(m_pgd.get());
        PGGlobals::setPGD3D(&m_d3d);
    }

    void TearDown() override
    {
        PGRunCache::discard();
        PGRunCache::initialize(cacheFile(), false);
        PatcherTextureHookConvertToCM::reset();
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
    }

    /// @brief The meshes as the directory lists them, with the given plugin uses for the stone.
    [[nodiscard]] std::unordered_map<std::filesystem::path,
                                     PGDirectory::NifCache>
    meshes(const PGRunCache::MeshUses& uses = { }) const
    {
        auto result = m_pgd->meshes();
        result.at(meshPath).meshUses = uses;
        return result;
    }

    /// @brief Writes the output a patch run of the stone would leave behind.
    void writeOutputs() const
    {
        TestUtil::writeFile(outDir() / meshPath, "patched mesh");
        TestUtil::DDSBuilder::writeDDS(outDir() / L"textures\\a\\stone_m.dds", 16, 16);
    }

    /// @brief Records a complete patch run of the stone, as the patcher would while patching it.
    void recordRun() const
    {
        PGRunCache::MeshRecorder recorder(meshPath);
        std::ignore = m_pgd->isFile(L"textures\\a\\stone.dds"); // queried for the recorder
        std::ignore = m_pgd->isFile(L"textures\\a\\missing.dds"); // queried for the recorder
        std::ignore = m_pgd->file(meshPath); // queried for the recorder
        PGRunCache::recordTextureType(L"textures\\a\\stone_p.dds", TextureType::Height);
        PGRunCache::recordTextureAttribute(L"textures\\a\\stone_m.dds", TextureAttribute::CMMetalness, true);
        PGRunCache::recordTextureAttributes(L"textures\\a\\stone_m.dds",
                                            { TextureAttribute::CMMetalness, TextureAttribute::CMGlossiness });
        PGRunCache::recordTexMatch(L"textures\\a\\stone",
                                   TextureType::Height,
                                   PGNIFUtil::texMatch(L"textures\\a\\stone",
                                                       TextureType::Height,
                                                       m_pgd->textureMapConst(TextureSlots::Parallax)));
        PGTypes::TextureSet slots;
        slots[0] = L"textures\\a\\stone.dds";
        PGRunCache::recordMatches(
            slots,
            false,
            PGPlugin::ModelRecordType::StaticObject,
            PGPatcher::computeMatchesDigest(meshPath, slots, false, PGPlugin::ModelRecordType::StaticObject));
        PGRunCache::recordHookRegistration(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds");
        PGRunCache::recordOutputFile(meshPath, 12);
        Logger::warn(L"{}", TestUtil::uniqueMessageW(L"recorded warning"));
        Logger::info(L"{}", TestUtil::uniqueMessageW(L"recorded info")); // not kept

        recorder.setUses({ { s_use, PGPlugin::MeshUseAttributes { } } });
        PGMeshPermutationTracker::MeshResult result;
        result.meshPath = meshPath;
        result.altTexResults.emplace_back(s_use, std::unordered_map<unsigned, PGTypes::TextureSet> { });
        result.idxCorrections = { { 0, 0 } };
        recorder.setMeshResults({ result });
        recorder.setDiff(11, 22);
        PGPatcher::MeshMeta meta;
        meta.globalPatchersApplied = { "ParticleLightsToLP" };
        meta.formKeys = { s_use };
        PGPatcher::MatchMeta match;
        match.shader = PGEnums::ShapeShader::ComplexMaterial;
        match.matchedPath = L"textures\\a\\stone_m.dds";
        meta.shapeMeta[0].blockID = 3;
        meta.shapeMeta[0].shapeName = "Stone";
        meta.shapeMeta[0].prePatchersApplied = { "FixTextureSlotCount" };
        meta.shapeMeta[0].matches[s_use].push_back(match);
        recorder.setMeta(meta);
        recorder.commit();
    }
};

TEST_F(PGRunCacheTest,
       ADisabledCacheIsInert)
{
    PGRunCache::initialize(cacheFile(), false);
    EXPECT_FALSE(PGRunCache::isEnabled());
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_FALSE(PGRunCache::arePreviousRecordsValid());
    EXPECT_FALSE(PGRunCache::arePreviousMeshUsesValid());

    const auto identity = m_pgd->fileIdentity(meshPath);
    std::vector<PGTypes::TextureVote> votes;
    PGRunCache::storeMeshVotes(meshPath, identity, { { .texture = L"textures\\a\\stone.dds" } });
    EXPECT_FALSE(PGRunCache::tryGetCachedMeshVotes(meshPath, identity, votes));
    PGTypes::CMClassification cm;
    PGRunCache::storeCMClassification(L"textures\\a\\stone_m.dds", identity, { .isCM = true });
    EXPECT_FALSE(PGRunCache::tryGetCachedCMClassification(L"textures\\a\\stone_m.dds", identity, cm));
    PGRunCache::MeshUses uses;
    EXPECT_FALSE(PGRunCache::tryGetCachedMeshUses(meshPath, uses));
    EXPECT_EQ(PGRunCache::previousRecord(meshPath), nullptr);
    EXPECT_FALSE(PGRunCache::tryReuseHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(PGRunCache::evaluateMeshes(meshes(), false).empty());

    // Recording happens, committing does not.
    recordRun();
    EXPECT_TRUE(PGRunCache::finishRun(true));
    EXPECT_FALSE(std::filesystem::exists(cacheFile()));
    EXPECT_FALSE(PGRunCache::isUpdateAvailable(outDir()));
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_FALSE(PGRunCache::isRecording());
}

TEST_F(PGRunCacheTest,
       ARunIsRecordedSavedAndReusedByTheNextOne)
{
    PGRunCache::initialize(cacheFile(), true, true);
    EXPECT_TRUE(PGRunCache::isEnabled());
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, "Generating the output from scratch"));
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);

    const auto meshIdentity = m_pgd->fileIdentity(meshPath);
    PGRunCache::storeMeshVotes(meshPath,
                               meshIdentity,
                               {
                                   {
                                       .texture = L"textures\\a\\stone.dds",
                                       .slot = TextureSlots::Diffuse,
                                       .type = TextureType::Diffuse,
                                   },
                               });
    PGRunCache::storeCMClassification(
        L"textures\\a\\stone_m.dds",
        m_pgd->fileIdentity(L"textures\\a\\stone_m.dds"),
        { .isCM = true, .hasEnvMask = true, .hasGlossiness = false, .hasMetalness = true });
    // Identities of files that are not there are ignored.
    PGRunCache::storeMeshVotes(L"meshes\\a\\missing.nif", m_pgd->fileIdentity(L"meshes\\a\\missing.nif"), { });

    writeOutputs();
    recordRun();
    PGRunCache::appendDeferredMessage(meshPath, spdlog::level::err, TestUtil::uniqueMessageW(L"deferred error"));
    PGRunCache::appendDeferredMessage(L"meshes\\a\\other.nif", spdlog::level::err, L"ignored");
    PGRunCache::recordHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds", L"textures\\a\\stone_m.dds");
    PGRunCache::recordHookOutput(HookKind::FixSSS, L"textures\\a\\stone.dds", L"textures\\a\\not_written_s.dds");

    ASSERT_TRUE(PGRunCache::finishRun(true));
    ASSERT_TRUE(std::filesystem::exists(cacheFile()));
    EXPECT_TRUE(PGRunCache::isUpdateAvailable(outDir()));
    EXPECT_TRUE(PGRunCache::hasPreviousRun()); // the run that just finished

    // The next run loads the file.
    PGRunCache::initialize(cacheFile(), true, false);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, "Previous output found in the output directory"));
    ASSERT_TRUE(PGRunCache::hasPreviousRun());
    EXPECT_FALSE(PGRunCache::arePreviousRecordsValid());
    EXPECT_FALSE(PGRunCache::arePreviousMeshUsesValid());
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);
    EXPECT_TRUE(PGRunCache::arePreviousRecordsValid());
    EXPECT_TRUE(PGRunCache::arePreviousMeshUsesValid());

    // Classification caches.
    std::vector<PGTypes::TextureVote> votes;
    EXPECT_TRUE(PGRunCache::tryGetCachedMeshVotes(meshPath, meshIdentity, votes));
    ASSERT_EQ(votes.size(), 1U);
    EXPECT_EQ(votes[0].texture, L"textures\\a\\stone.dds");
    BethesdaDirectory::FileIdentity changed = meshIdentity;
    changed.size++;
    EXPECT_FALSE(PGRunCache::tryGetCachedMeshVotes(meshPath, changed, votes));
    PGTypes::CMClassification cm;
    EXPECT_TRUE(PGRunCache::tryGetCachedCMClassification(
        L"textures\\a\\stone_m.dds", m_pgd->fileIdentity(L"textures\\a\\stone_m.dds"), cm));
    EXPECT_TRUE(cm.isCM);
    EXPECT_TRUE(cm.hasMetalness);
    EXPECT_FALSE(cm.hasGlossiness);
    EXPECT_FALSE(PGRunCache::tryGetCachedCMClassification(
        L"textures\\a\\stone.dds", m_pgd->fileIdentity(L"textures\\a\\stone.dds"), cm));
    PGRunCache::MeshUses uses;
    EXPECT_TRUE(PGRunCache::tryGetCachedMeshUses(meshPath, uses));
    EXPECT_TRUE(uses.empty()); // the directory knew no plugin uses

    // The record of the mesh.
    const auto* record = PGRunCache::previousRecord(meshPath);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(PGRunCache::previousRecord(L"meshes\\a\\other.nif"), nullptr);
    ASSERT_EQ(record->uses.size(), 1U);
    EXPECT_EQ(record->uses[0].first, s_use);
    EXPECT_EQ(record->fileIdentityDeps.size(), 1U);
    EXPECT_EQ(record->fileExistsDeps.size(), 2U);
    EXPECT_EQ(record->textureTypeDeps.size(), 1U);
    EXPECT_EQ(record->textureAttributeDeps.size(), 1U);
    EXPECT_EQ(record->textureAttributesDeps.size(), 1U);
    EXPECT_EQ(record->texMatchDeps.size(), 1U);
    EXPECT_EQ(record->matchesDeps.size(), 1U);
    ASSERT_EQ(record->outputFiles.size(), 1U);
    EXPECT_EQ(record->outputFiles[0].first, L"meshes\\a\\stone.nif");
    EXPECT_EQ(record->outputFiles[0].second.size, 12U);
    EXPECT_NE(record->outputFiles[0].second.mtime, 0);
    ASSERT_EQ(record->meshResults.size(), 1U);
    EXPECT_EQ(record->meshResults[0].idxCorrections, (std::unordered_map<int, int> { { 0, 0 } }));
    EXPECT_TRUE(record->hasDiff);
    EXPECT_EQ(record->crc32Original, 11U);
    EXPECT_EQ(record->crc32Patched, 22U);
    ASSERT_EQ(record->hookRegistrations.size(), 1U);
    EXPECT_EQ(record->hookRegistrations[0].first, HookKind::ConvertToCM);
    ASSERT_EQ(record->messages.size(), 2U);
    EXPECT_EQ(record->messages[0].level, spdlog::level::warn);
    EXPECT_EQ(record->messages[1].level, spdlog::level::err);
    EXPECT_EQ(record->meta.globalPatchersApplied, (std::vector<std::string> { "ParticleLightsToLP" }));
    ASSERT_EQ(record->meta.shapeMeta.size(), 1U);
    EXPECT_EQ(record->meta.shapeMeta[0].second.shapeName, "Stone");
    ASSERT_EQ(record->meta.shapeMeta[0].second.matches.size(), 1U);
    EXPECT_EQ(record->meta.shapeMeta[0].second.matches[0].second[0].matchedPath, L"textures\\a\\stone_m.dds");

    // Nothing changed, so the mesh is skippable; stale outputs are removed, hook outputs are kept for now.
    TestUtil::writeFile(outDir() / L"meshes\\a\\stale.nif", "stale");
    PGRunCache::snapshotOutputDirectory();
    const PGRunCache::MeshUses recordedUses { { s_use, PGPlugin::MeshUseAttributes { } } };
    const auto skippable = PGRunCache::evaluateMeshes(meshes(recordedUses), false);
    EXPECT_EQ(skippable, (std::unordered_set<std::filesystem::path> { meshPath }));
    PGRunCache::pruneStaleOutputs(skippable);
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"meshes\\a\\stale.nif"));
    EXPECT_TRUE(std::filesystem::exists(outDir() / meshPath));
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"textures\\a\\stone_m.dds"));

    // The generated texture is reused when its source is unchanged.
    EXPECT_TRUE(PGRunCache::tryReuseHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\a\\stone_m.dds"));
    EXPECT_FALSE(PGRunCache::tryReuseHookOutput(HookKind::FixSSS, L"textures\\a\\stone_p.dds"));
    EXPECT_FALSE(PGRunCache::tryReuseHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone.dds"));

    // Replaying the mesh keeps its record, and the hook output stays needed.
    PGRunCache::carryOverRecord(meshPath);
    PGRunCache::carryOverRecord(L"meshes\\a\\other.nif");
    PGRunCache::finalizeHooks();
    EXPECT_TRUE(std::filesystem::exists(outDir() / L"textures\\a\\stone_m.dds"));
    EXPECT_TRUE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(L"textures\\a\\stone"));
    ASSERT_TRUE(PGRunCache::finishRun(true));
    PGRunCache::initialize(cacheFile(), true, false);
    PGRunCache::setConfigFingerprint(42);
    ASSERT_NE(PGRunCache::previousRecord(meshPath), nullptr);
    EXPECT_EQ(PGRunCache::previousRecord(meshPath)->crc32Patched, 22U);

    // Different plugin uses, a different configuration or a missing output mean re-patching.
    PGRunCache::snapshotOutputDirectory();
    EXPECT_TRUE(
        PGRunCache::evaluateMeshes(meshes({ { s_use, PGPlugin::MeshUseAttributes { .isWeighted = true } } }), false)
            .empty());
    EXPECT_TRUE(PGRunCache::evaluateMeshes(meshes(), false).empty()); // no uses at all
    EXPECT_FALSE(PGRunCache::evaluateMeshes(meshes(recordedUses), false).empty());
    std::filesystem::remove(outDir() / meshPath);
    PGRunCache::snapshotOutputDirectory();
    EXPECT_TRUE(PGRunCache::evaluateMeshes(meshes(recordedUses), false).empty());
    PGRunCache::setConfigFingerprint(43);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, "Patching configuration changed since the previous run"));
    EXPECT_FALSE(PGRunCache::arePreviousRecordsValid());

    // Discarding forgets the previous run and deletes the file.
    PGRunCache::discard();
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_FALSE(std::filesystem::exists(cacheFile()));
    EXPECT_FALSE(PGRunCache::isUpdateAvailable(outDir()));
}

TEST_F(PGRunCacheTest,
       ObjectBoundsOfTheResultsSurviveTheCache)
{
    PGRunCache::initialize(cacheFile(), true, true);
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);
    writeOutputs();

    const PGTypes::ObjectBounds meshBounds { .min = { -5, 0, 3 }, .max = { 9, 8, 7 } };
    {
        PGRunCache::MeshRecorder recorder(meshPath);
        std::ignore = m_pgd->file(meshPath); // queried for the recorder
        PGRunCache::recordOutputFile(meshPath, 12);
        recorder.setUses({ { s_use, PGPlugin::MeshUseAttributes { } } });

        PGMeshPermutationTracker::MeshResult saved;
        saved.meshPath = meshPath;
        saved.altTexResults.emplace_back(s_use, std::unordered_map<unsigned, PGTypes::TextureSet> { });
        saved.objectBounds = meshBounds;
        PGMeshPermutationTracker::MeshResult withoutBounds;
        withoutBounds.meshPath = L"meshes\\a\\other.nif";
        recorder.setMeshResults({ saved, withoutBounds });
        recorder.setMeta(PGPatcher::MeshMeta { });
        recorder.commit();
    }
    ASSERT_TRUE(PGRunCache::finishRun(true));

    PGRunCache::initialize(cacheFile(), true, false);
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);
    const auto* record = PGRunCache::previousRecord(meshPath);
    ASSERT_NE(record, nullptr);
    ASSERT_EQ(record->meshResults.size(), 2U);
    EXPECT_EQ(record->meshResults[0].objectBounds, meshBounds);
    EXPECT_EQ(record->meshResults[1].objectBounds, std::nullopt);
}

TEST_F(PGRunCacheTest,
       UnusedGeneratedTexturesAreDeletedByTheTexturePhase)
{
    PGRunCache::initialize(cacheFile(), true, true);
    PGRunCache::setConfigFingerprint(1);
    writeOutputs();
    PGRunCache::recordHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds", L"textures\\a\\stone_m.dds");
    ASSERT_TRUE(PGRunCache::finishRun(true));

    // The next run patches the mesh again without registering the hook, so the texture is not needed anymore.
    PGRunCache::initialize(cacheFile(), true, false);
    PGRunCache::setConfigFingerprint(1);
    PGRunCache::snapshotOutputDirectory();
    PGRunCache::finalizeHooks();
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"textures\\a\\stone_m.dds"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, "removed 1 generated textures that are no longer needed"));
}

TEST_F(PGRunCacheTest,
       CriticalMessagesInvalidateTheRecordAndRecordersDoNotNest)
{
    PGRunCache::initialize(cacheFile(), true, true);
    {
        PGRunCache::MeshRecorder recorder(meshPath);
        EXPECT_THROW(const PGRunCache::MeshRecorder nested(L"meshes\\a\\other.nif"), std::runtime_error);
        Logger::critical(L"{}", TestUtil::uniqueMessageW(L"critical"));
        recorder.commit();
    }
    ASSERT_TRUE(PGRunCache::finishRun(true));
    PGRunCache::initialize(cacheFile(), true, false);
    ASSERT_TRUE(PGRunCache::hasPreviousRun());
    EXPECT_EQ(PGRunCache::previousRecord(meshPath), nullptr);
}

TEST_F(PGRunCacheTest,
       LoadingRejectsMissingAndBrokenFiles)
{
    PGRunCache::initialize(cacheFile(), true, false);
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "No previous output found in the output directory"));

    TestUtil::writeFile(cacheFile(), "this is not a cache file");
    EXPECT_FALSE(PGRunCache::isUpdateAvailable(outDir()));
    PGRunCache::initialize(cacheFile(), true, false);
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "The update cache in the output directory could not be used"));

    TestUtil::writeFile(cacheFile(), "");
    PGRunCache::initialize(cacheFile(), true, false);
    EXPECT_FALSE(PGRunCache::hasPreviousRun());

    EXPECT_FALSE(PGRunCache::isUpdateAvailable(L""));
    EXPECT_FALSE(PGRunCache::isUpdateAvailable(m_dir / L"Nowhere"));

    // Regenerating from scratch leaves an existing cache alone until the run finishes.
    PGRunCache::initialize(cacheFile(), true, true);
    EXPECT_TRUE(TestLog::contains(spdlog::level::info, "Regenerating the output from scratch"));
    EXPECT_FALSE(PGRunCache::hasPreviousRun());
    EXPECT_TRUE(std::filesystem::exists(cacheFile()));
}

TEST_F(PGRunCacheTest,
       ReplayHelpersRebuildMessagesAndMetadata)
{
    PGRunCache::MeshRecord record;
    const auto warning = TestUtil::uniqueMessageW(L"replayed warning");
    const auto error = TestUtil::uniqueMessageW(L"replayed error");
    const auto info = TestUtil::uniqueMessageW(L"replayed info");
    record.messages = {
        { .level = spdlog::level::warn, .text = warning },
        { .level = spdlog::level::err, .text = error },
        { .level = spdlog::level::info, .text = info },
    };
    PGRunCache::replayMessages(record);
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, TestUtil::pathToUTF8(warning)));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, TestUtil::pathToUTF8(error)));
    EXPECT_FALSE(TestLog::contains(spdlog::level::info, TestUtil::pathToUTF8(info)));

    PGRunCache::MeshMetaRecord metaRecord;
    metaRecord.globalPatchersApplied = { "Global" };
    metaRecord.formKeys = { s_use };
    PGRunCache::MeshShapeMetaRecord shapeRecord;
    shapeRecord.blockID = 5;
    shapeRecord.shapeName = "Shape";
    shapeRecord.postPatchersApplied = { "Post" };
    PGRunCache::MatchMetaRecord matchRecord;
    matchRecord.modName = L"SomeMod"; // no mod manager, so it cannot be resolved
    matchRecord.shader = PGEnums::ShapeShader::VanillaParallax;
    matchRecord.shaderTransformTo = PGEnums::ShapeShader::ComplexMaterial;
    matchRecord.matchedPath = L"textures\\a\\stone_p.dds";
    matchRecord.resultTextureMods.emplace_back(TextureSlots::Parallax, L"SomeMod");
    shapeRecord.matches.emplace_back(s_use, std::vector<PGRunCache::MatchMetaRecord> { matchRecord });
    metaRecord.shapeMeta.emplace_back(2, shapeRecord);

    const auto meta = PGRunCache::buildMeshMeta(metaRecord);
    EXPECT_EQ(meta.globalPatchersApplied, (std::vector<std::string> { "Global" }));
    EXPECT_EQ(meta.formKeys, (std::vector<FormKey> { s_use }));
    ASSERT_TRUE(meta.shapeMeta.contains(2));
    EXPECT_EQ(meta.shapeMeta.at(2).blockID, 5U);
    EXPECT_EQ(meta.shapeMeta.at(2).shapeName, "Shape");
    EXPECT_EQ(meta.shapeMeta.at(2).postPatchersApplied, (std::vector<std::string> { "Post" }));
    ASSERT_TRUE(meta.shapeMeta.at(2).matches.contains(s_use));
    const auto& matches = meta.shapeMeta.at(2).matches.at(s_use);
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_FALSE(matches[0].mod);
    EXPECT_EQ(matches[0].shader, PGEnums::ShapeShader::VanillaParallax);
    EXPECT_EQ(matches[0].shaderTransformTo, PGEnums::ShapeShader::ComplexMaterial);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_p.dds");
    EXPECT_TRUE(matches[0].resultTextureMods.empty()); // unresolvable mods are dropped
}

TEST_F(PGRunCacheTest,
       TheInstalledObserverForwardsToTheCache)
{
    PGRunCache::initialize(cacheFile(), true, true);
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);

    // The classifications stored through the observer are the cache's: the next run reads them.
    const auto meshIdentity = m_pgd->fileIdentity(meshPath);
    const auto maskIdentity = m_pgd->fileIdentity(L"textures\\a\\stone_m.dds");
    {
        auto& observer = PGRunObserver::current();
        observer.storeMeshVotes(meshPath, meshIdentity, { { .texture = L"textures\\a\\stone.dds" } });
        observer.storeCMClassification(L"textures\\a\\stone_m.dds", maskIdentity, { .isCM = true });
        EXPECT_FALSE(observer.tryReuseHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds"));

        // The dependencies recorded through the observer land in the mesh record like the direct ones.
        writeOutputs();
        PGRunCache::MeshRecorder recorder(meshPath);
        observer.recordTextureType(L"textures\\a\\stone_p.dds", TextureType::Height);
        observer.recordTextureAttribute(L"textures\\a\\stone_m.dds", TextureAttribute::CMMetalness, true);
        observer.recordHookRegistration(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds");
        observer.recordOutputFile(meshPath, 12);
        {
            const PGRunObserver::SuspendRecording suspended;
            observer.recordTextureType(L"textures\\a\\stone.dds", TextureType::Diffuse); // not recorded
        }
        recorder.setUses({ { s_use, PGPlugin::MeshUseAttributes { } } });
        recorder.commit();
    }
    ASSERT_TRUE(PGRunCache::finishRun(true));

    PGRunCache::initialize(cacheFile(), true, false);
    PGRunCache::setConfigFingerprint(42);
    PGRunCache::setPluginFingerprint(7);
    auto& observer = PGRunObserver::current();
    std::vector<PGTypes::TextureVote> votes;
    EXPECT_TRUE(PGRunCache::tryGetCachedMeshVotes(meshPath, meshIdentity, votes));
    ASSERT_EQ(votes.size(), 1U);
    votes.clear();
    EXPECT_TRUE(observer.tryGetCachedMeshVotes(meshPath, meshIdentity, votes));
    EXPECT_EQ(votes.size(), 1U);
    PGTypes::CMClassification cm;
    EXPECT_TRUE(observer.tryGetCachedCMClassification(L"textures\\a\\stone_m.dds", maskIdentity, cm));
    EXPECT_TRUE(cm.isCM);

    const auto* record = PGRunCache::previousRecord(meshPath);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->textureTypeDeps.size(), 1U);
    EXPECT_EQ(record->textureAttributeDeps.size(), 1U);
    EXPECT_EQ(record->hookRegistrations.size(), 1U);
    EXPECT_EQ(record->outputFiles.size(), 1U);
}

}
