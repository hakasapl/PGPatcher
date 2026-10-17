#include "PGRunObserver.hpp"

#include "DDSBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "pgutil/PGTypes.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

namespace {

using HookKind = PGRunObserver::HookKind;

/**
 * @brief An observer that keeps what the texture hooks report and answers the reuse question as told.
 */
class RecordingObserver final : public PGRunObserver {
public:
    // The test reads the records and sets the answer directly.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    bool shouldReuse = false;
    std::vector<std::pair<HookKind, std::filesystem::path>> registrations;
    std::vector<std::pair<HookKind, std::filesystem::path>> reuseQueries;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> outputs;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    void recordHookRegistration(const HookKind& kind,
                                const std::filesystem::path& texPath) override
    {
        registrations.emplace_back(kind, texPath);
    }

    bool tryReuseHookOutput(const HookKind& kind,
                            const std::filesystem::path& texPath) override
    {
        reuseQueries.emplace_back(kind, texPath);
        return shouldReuse;
    }

    void recordHookOutput([[maybe_unused]] const HookKind& kind,
                          const std::filesystem::path& source,
                          const std::filesystem::path& output) override
    {
        outputs.emplace_back(source, output);
    }
};

/**
 * @brief A data folder with a height map and a diffuse, mapped by the global PGDirectory so the texture hooks can
 * register textures. The default observer is installed again after every test.
 */
class PGRunObserverTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    RecordingObserver m_observer;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    void SetUp() override
    {
        TestUtil::DDSBuilder::writeDDS(m_dir / L"Data\\textures\\a\\stone_p.dds", 16, 16);
        TestUtil::DDSBuilder::writeDDS(m_dir / L"Data\\textures\\a\\leaf.dds", 16, 16);
        m_pgd = std::make_unique<PGDirectory>(m_dir / L"Data", m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());
    }

    void TearDown() override
    {
        PGRunObserver::install(nullptr);
        PatcherTextureHookConvertToCM::reset();
        PatcherTextureHookFixSSS::reset();
        PGGlobals::setPGD(nullptr);
    }
};

TEST_F(PGRunObserverTest,
       TheDefaultObserverRecordsNothingAndReplaysNothing)
{
    PGRunObserver::install(nullptr);
    auto& observer = PGRunObserver::current();

    EXPECT_FALSE(observer.tryReuseHookOutput(HookKind::ConvertToCM, L"textures\\a\\stone_p.dds"));
    observer.recordHookRegistration(HookKind::FixSSS, L"textures\\a\\leaf.dds");
    observer.recordHookOutput(HookKind::FixSSS, L"textures\\a\\leaf.dds", L"textures\\a\\leaf_s.dds");
    observer.recordTextureReadFailure(L"textures\\a\\leaf.dds");

    std::vector<PGTypes::TextureVote> votes;
    EXPECT_FALSE(observer.tryGetCachedMeshVotes(L"meshes\\a\\stone.nif", { }, votes));
    PGRunObserver::MeshUses uses;
    EXPECT_FALSE(observer.tryGetCachedMeshUses(L"meshes\\a\\stone.nif", uses));
    PGTypes::CMClassification classification;
    EXPECT_FALSE(observer.tryGetCachedCMClassification(L"textures\\a\\stone_m.dds", { }, classification));

    // The hooks register through the default observer and schedule the generation.
    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds");
    EXPECT_TRUE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\a\\stone_m.dds"));
}

TEST_F(PGRunObserverTest,
       TheInstalledObserverSeesTheHookRegistrationsAndCanReplayThem)
{
    PGRunObserver::install(&m_observer);
    EXPECT_EQ(&PGRunObserver::current(), &m_observer);

    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds");
    PatcherTextureHookFixSSS::addToProcessList(L"textures\\a\\leaf.dds");
    ASSERT_EQ(m_observer.registrations.size(), 2U);
    EXPECT_EQ(m_observer.registrations[0].first, HookKind::ConvertToCM);
    EXPECT_EQ(m_observer.registrations[0].second, L"textures\\a\\stone_p.dds");
    EXPECT_EQ(m_observer.registrations[1].first, HookKind::FixSSS);
    EXPECT_EQ(m_observer.registrations[1].second, L"textures\\a\\leaf.dds");
    EXPECT_EQ(m_observer.reuseQueries.size(), 2U);
    EXPECT_TRUE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(PatcherTextureHookFixSSS::isInProcessList(L"textures\\a\\leaf.dds"));

    // When the observer replays the output of a previous run, nothing is scheduled.
    PatcherTextureHookConvertToCM::reset();
    m_observer.shouldReuse = true;
    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds");
    EXPECT_EQ(m_observer.registrations.size(), 3U);
    EXPECT_FALSE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));

    // Uninstalling restores the default observer, which schedules again.
    PGRunObserver::install(nullptr);
    EXPECT_NE(&PGRunObserver::current(), &m_observer);
    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds");
    EXPECT_EQ(m_observer.registrations.size(), 3U);
    EXPECT_TRUE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
}

}
