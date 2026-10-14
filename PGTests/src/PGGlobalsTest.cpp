#include "PGGlobals.hpp"

#include "FakeGame.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGModManager.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <filesystem>
#include <stdexcept>

namespace {

class PGGlobalsTest : public ::testing::Test {
protected:
    void SetUp() override { clear(); }
    void TearDown() override { clear(); }

    static void clear()
    {
        PGGlobals::setBG(nullptr);
        PGGlobals::setPGD(nullptr);
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGMM(nullptr);
    }
};

TEST_F(PGGlobalsTest,
       GettersThrowUntilSet)
{
    EXPECT_FALSE(PGGlobals::isBGSet());
    EXPECT_FALSE(PGGlobals::isPGDSet());
    EXPECT_FALSE(PGGlobals::isPGD3DSet());
    EXPECT_FALSE(PGGlobals::isPGMMSet());
    EXPECT_THROW(PGGlobals::bg(), std::runtime_error);
    EXPECT_THROW(PGGlobals::pgd(), std::runtime_error);
    EXPECT_THROW(PGGlobals::pgD3D(), std::runtime_error);
    EXPECT_THROW(PGGlobals::pgmm(), std::runtime_error);
}

TEST_F(PGGlobalsTest,
       SettersStoreThePointers)
{
    const TestUtil::FakeGame fake;
    BethesdaGame game = fake.game();
    PGDirectory pgd(fake.dataDir(), fake.gameDir() / L"out");
    PGD3D pgd3d;
    PGModManager pgmm(PGModManager::ModManagerType::None);

    PGGlobals::setBG(&game);
    PGGlobals::setPGD(&pgd);
    PGGlobals::setPGD3D(&pgd3d);
    PGGlobals::setPGMM(&pgmm);

    EXPECT_TRUE(PGGlobals::isBGSet());
    EXPECT_TRUE(PGGlobals::isPGDSet());
    EXPECT_TRUE(PGGlobals::isPGD3DSet());
    EXPECT_TRUE(PGGlobals::isPGMMSet());
    EXPECT_EQ(PGGlobals::bg(), &game);
    EXPECT_EQ(PGGlobals::pgd(), &pgd);
    EXPECT_EQ(PGGlobals::pgD3D(), &pgd3d);
    EXPECT_EQ(PGGlobals::pgmm(), &pgmm);

    // Clearing one pointer leaves the others alone.
    PGGlobals::setPGD(nullptr);
    EXPECT_FALSE(PGGlobals::isPGDSet());
    EXPECT_THROW(PGGlobals::pgd(), std::runtime_error);
    EXPECT_TRUE(PGGlobals::isBGSet());
    EXPECT_TRUE(PGGlobals::isPGD3DSet());
    EXPECT_TRUE(PGGlobals::isPGMMSet());
}

TEST_F(PGGlobalsTest,
       FileSaverIsOneQueueThatRunsTasks)
{
    auto& first = PGGlobals::fileSaver();
    auto const& second = PGGlobals::fileSaver();
    EXPECT_EQ(&first, &second);

    std::atomic<int> ran { 0 };
    first.queueTask([&ran] { ran++; });
    first.waitForCompletion();
    EXPECT_EQ(ran, 1);
    EXPECT_FALSE(first.isShutdown());
}

TEST_F(PGGlobalsTest,
       FoldersToMapListsThePgFolders)
{
    const auto& folders = PGGlobals::s_foldersToMap;
    EXPECT_EQ(folders.size(), 5U);
    for (const auto* folder : { "meshes", "textures", "pbrnifpatcher", "lightplacer", "calientetools" })
        EXPECT_TRUE(folders.contains(std::filesystem::path(folder))) << folder;
}

}
