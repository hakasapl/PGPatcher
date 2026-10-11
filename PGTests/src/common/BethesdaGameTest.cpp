#include "common/BethesdaGame.hpp"

#include "FakeGame.hpp"
#include "TestUtil.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using GameType = BethesdaGame::GameType;
using TestUtil::FakeGame;

TEST(BethesdaGameTest,
     ConstructorUsesTheGivenPaths)
{
    const FakeGame fake;
    const BethesdaGame game = fake.game();

    EXPECT_EQ(game.gameType(), GameType::SkyrimSE);
    EXPECT_EQ(game.gamePath(), fake.gameDir());
    EXPECT_EQ(game.gameDataPath(), fake.dataDir());
    EXPECT_EQ(game.pluginsFile(), fake.pluginsFile());

    const auto inis = game.iniPaths();
    EXPECT_EQ(inis.ini, fake.documentsDir() / L"skyrim.ini");
    EXPECT_EQ(inis.iniPrefs, fake.documentsDir() / L"skyrimprefs.ini");
    EXPECT_EQ(inis.iniCustom, fake.documentsDir() / L"skyrimcustom.ini");
}

TEST(BethesdaGameTest,
     IniNamesDependOnTheGame)
{
    const FakeGame enderal(GameType::EnderalSE);
    const auto enderalInis = enderal.game().iniPaths();
    EXPECT_EQ(enderalInis.ini, enderal.documentsDir() / L"enderal.ini");
    EXPECT_EQ(enderalInis.iniPrefs, enderal.documentsDir() / L"enderalprefs.ini");
    EXPECT_EQ(enderalInis.iniCustom, enderal.documentsDir() / L"enderalcustom.ini");

    const FakeGame vr(GameType::SkyrimVR);
    EXPECT_EQ(vr.game().iniPaths().ini, vr.documentsDir() / L"skyrim.ini");

    const FakeGame gog(GameType::SkyrimGOG);
    EXPECT_EQ(gog.game().iniPaths().iniCustom, gog.documentsDir() / L"skyrimcustom.ini");
}

TEST(BethesdaGameTest,
     ConstructorRejectsInvalidInstalls)
{
    const FakeGame fake;

    // Missing game folder.
    EXPECT_THROW(BethesdaGame(GameType::SkyrimSE, fake.gameDir() / L"missing", fake.appDataDir(), fake.documentsDir()),
                 std::runtime_error);

    // A folder without a Data folder.
    EXPECT_THROW(BethesdaGame(GameType::SkyrimSE, fake.documentsDir(), fake.appDataDir(), fake.documentsDir()),
                 std::runtime_error);

    // The master of another game is not enough.
    EXPECT_THROW(BethesdaGame(GameType::SkyrimVR, fake.gameDir(), fake.appDataDir(), fake.documentsDir()),
                 std::runtime_error);
    EXPECT_THROW(BethesdaGame(GameType::EnderalSE, fake.gameDir(), fake.appDataDir(), fake.documentsDir()),
                 std::runtime_error);

    // plugins.txt must exist in the given AppData folder, not somewhere else.
    EXPECT_THROW(BethesdaGame(GameType::SkyrimSE, fake.gameDir(), fake.documentsDir(), fake.documentsDir()),
                 std::runtime_error);

    EXPECT_THROW(BethesdaGame(GameType::Unknown, fake.gameDir(), fake.appDataDir(), fake.documentsDir()),
                 std::runtime_error);

    // Without a game path the registry is asked, and the GOG release has no registry key.
    EXPECT_THROW(BethesdaGame(GameType::SkyrimGOG, "", fake.appDataDir(), fake.documentsDir()), std::runtime_error);

    // The GOG release shares Skyrim.esm with the Steam release.
    EXPECT_NO_THROW(BethesdaGame(GameType::SkyrimGOG, fake.gameDir(), fake.appDataDir(), fake.documentsDir()));
}

TEST(BethesdaGameTest,
     IsGamePathValidChecksTheMasterAndPluginsFile)
{
    const FakeGame fake;
    EXPECT_TRUE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::SkyrimSE, fake.appDataDir()));
    EXPECT_TRUE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::SkyrimGOG, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::SkyrimVR, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::EnderalSE, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::Unknown, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid("", GameType::SkyrimSE, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir() / L"missing", GameType::SkyrimSE, fake.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.dataDir(), GameType::SkyrimSE, fake.appDataDir()));

    // The AppData folder has to hold plugins.txt.
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::SkyrimSE, fake.documentsDir()));
    std::filesystem::remove(fake.pluginsFile());
    EXPECT_FALSE(BethesdaGame::isGamePathValid(fake.gameDir(), GameType::SkyrimSE, fake.appDataDir()));

    // A master that is a folder does not count.
    const FakeGame folderMaster(GameType::SkyrimVR);
    std::filesystem::remove(folderMaster.dataDir() / L"SkyrimVR.esm");
    std::filesystem::create_directories(folderMaster.dataDir() / L"SkyrimVR.esm");
    EXPECT_FALSE(BethesdaGame::isGamePathValid(folderMaster.gameDir(), GameType::SkyrimVR, folderMaster.appDataDir()));

    const FakeGame enderal(GameType::EnderalSE);
    EXPECT_TRUE(BethesdaGame::isGamePathValid(enderal.gameDir(), GameType::EnderalSE, enderal.appDataDir()));
    EXPECT_FALSE(BethesdaGame::isGamePathValid(enderal.gameDir(), GameType::SkyrimSE, enderal.appDataDir()));
}

TEST(BethesdaGameTest,
     ActivePluginsListsMastersCreationClubAndStarredPlugins)
{
    FakeGame fake;
    fake.addPlugin(L"Update.esm", false);
    fake.addPlugin(L"Dawnguard.esm", false);
    fake.addPlugin(L"Dragonborn.esm", false);
    fake.addPlugin(L"ccBGSSSE001-Fish.esm", false);
    fake.addPlugin(L"ModA.esp");
    fake.addPlugin(L"Inactive.esp", false);
    fake.appendPluginsLine("Inactive.esp"); // listed without a star
    fake.appendPluginsLine("*moda.esp"); // same plugin again in another case
    fake.appendPluginsLine("*Missing.esp"); // not in the data folder
    fake.appendPluginsLine("# *Commented.esp");
    fake.addPlugin(L"ModB.esp");
    fake.appendPluginsLine("*Update.esm"); // already listed as a master
    TestUtil::writeFile(fake.gameDir() / L"Skyrim.ccc", "# Creation Club\nccBGSSSE001-Fish.esm\nccMissing.esl\n");

    const BethesdaGame game = fake.game();
    const std::vector<std::wstring> expected {
        L"Skyrim.esm",           L"Update.esm", L"Dawnguard.esm", L"Dragonborn.esm",
        L"ccBGSSSE001-Fish.esm", L"ModA.esp",   L"ModB.esp",
    };
    EXPECT_EQ(game.activePlugins(), expected);

    const std::vector<std::wstring> trimmed {
        L"Skyrim", L"Update", L"Dawnguard", L"Dragonborn", L"ccBGSSSE001-Fish", L"ModA", L"ModB",
    };
    EXPECT_EQ(game.activePlugins(true), trimmed);

    const std::vector<std::wstring> lower {
        L"skyrim.esm",           L"update.esm", L"dawnguard.esm", L"dragonborn.esm",
        L"ccbgssse001-fish.esm", L"moda.esp",   L"modb.esp",
    };
    EXPECT_EQ(game.activePlugins(false, true), lower);

    const std::vector<std::wstring> trimmedLower {
        L"skyrim", L"update", L"dawnguard", L"dragonborn", L"ccbgssse001-fish", L"moda", L"modb",
    };
    EXPECT_EQ(game.activePlugins(true, true), trimmedLower);
}

TEST(BethesdaGameTest,
     ActivePluginsOnlyListsMastersThatExist)
{
    const FakeGame fake;
    const BethesdaGame game = fake.game();
    EXPECT_EQ(game.activePlugins(), (std::vector<std::wstring> { L"Skyrim.esm" }));

    FakeGame vr(GameType::SkyrimVR);
    vr.addPlugin(L"Skyrim.esm", false);
    vr.addPlugin(L"HearthFires.esm", false);
    EXPECT_EQ(vr.game().activePlugins(),
              (std::vector<std::wstring> { L"Skyrim.esm", L"HearthFires.esm", L"SkyrimVR.esm" }));

    // SkyrimVR.esm is only a master for Skyrim VR.
    FakeGame se;
    se.addPlugin(L"SkyrimVR.esm", false);
    EXPECT_EQ(se.game().activePlugins(), (std::vector<std::wstring> { L"Skyrim.esm" }));
}

TEST(BethesdaGameTest,
     ActivePluginsThrowsWithoutPluginsFile)
{
    const FakeGame fake;
    const BethesdaGame game = fake.game();
    std::filesystem::remove(fake.pluginsFile());
    EXPECT_THROW(std::ignore = game.activePlugins(), std::runtime_error);
}

TEST(BethesdaGameTest,
     GameTypeStringsRoundTrip)
{
    EXPECT_EQ(BethesdaGame::strFromGameType(GameType::SkyrimSE), "Skyrim SE");
    EXPECT_EQ(BethesdaGame::strFromGameType(GameType::SkyrimGOG), "Skyrim GOG");
    EXPECT_EQ(BethesdaGame::strFromGameType(GameType::SkyrimVR), "Skyrim VR");
    EXPECT_EQ(BethesdaGame::strFromGameType(GameType::EnderalSE), "Enderal SE");
    EXPECT_EQ(BethesdaGame::strFromGameType(GameType::Unknown), "Skyrim SE");

    for (const auto type : BethesdaGame::gameTypes())
        EXPECT_EQ(BethesdaGame::gameTypeFromStr(BethesdaGame::strFromGameType(type)), type);

    EXPECT_EQ(BethesdaGame::gameTypeFromStr("Oblivion"), GameType::SkyrimSE);
    EXPECT_EQ(BethesdaGame::gameTypeFromStr(""), GameType::SkyrimSE);
    EXPECT_EQ(BethesdaGame::gameTypeFromStr("skyrim se"), GameType::SkyrimSE); // case matters, so this is the default
}

TEST(BethesdaGameTest,
     GameTypesListsEverySupportedGame)
{
    const std::vector<GameType> expected {
        GameType::SkyrimSE,
        GameType::SkyrimGOG,
        GameType::SkyrimVR,
        GameType::EnderalSE,
    };
    EXPECT_EQ(BethesdaGame::gameTypes(), expected);
}

TEST(BethesdaGameTest,
     FindGamePathFromSteamNeedsARegistryKey)
{
    // The GOG release and the unknown game have no registry key.
    EXPECT_TRUE(BethesdaGame::findGamePathFromSteam(GameType::SkyrimGOG).empty());
    EXPECT_TRUE(BethesdaGame::findGamePathFromSteam(GameType::Unknown).empty());

    // Whatever the registry says for the Steam releases, it is a folder when it says anything.
    for (const auto type : { GameType::SkyrimSE, GameType::SkyrimVR, GameType::EnderalSE }) {
        const auto path = BethesdaGame::findGamePathFromSteam(type);
        if (!path.empty())
            EXPECT_TRUE(std::filesystem::is_directory(path)) << TestUtil::pathToUTF8(path);
    }
}

}
