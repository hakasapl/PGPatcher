#include "common/BethesdaDirectory.hpp"

#include "BSABuilder.hpp"
#include "FakeGame.hpp"
#include "FakeMO2.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"
#include "common/MO2DataFolder.hpp"
#include "util/FileUtil.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>
#include <windows.h>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::BSABuilder;
using TestUtil::FakeGame;
using TestUtil::FakeMO2;
using TestUtil::TempDir;

std::string bytesToString(const std::vector<std::byte>& bytes)
{
    std::string out;
    out.reserve(bytes.size());
    for (const auto byte : bytes)
        out.push_back(static_cast<char>(byte));
    return out;
}

std::set<std::wstring> mappedPaths(const BethesdaDirectory& dir)
{
    std::set<std::wstring> paths;
    for (const auto& [path, file] : dir.fileMap())
        paths.insert(path.wstring());
    return paths;
}

/**
 * @brief A game whose data folder has archives of several plugins, an INI listed archive and loose files.
 *
 * Load order of the archives: Skyrim - Misc.bsa (INI), ModA.bsa, ModA - Textures.bsa, ModB.bsa. Unloaded.bsa belongs
 * to no plugin, and neither does ModA².bsa (a superscript is not a digit). meshes\shared.nif is in every archive,
 * meshes\loose.nif is archived and loose.
 */
class ArchivedGame {
private:
    FakeGame m_game;

public:
    ArchivedGame()
    {
        m_game.addPlugin(L"ModA.esp");
        m_game.addPlugin(L"ModB.esp");
        m_game.writeMainINI("[Archive]\nsResourceArchiveList=Skyrim - Misc.bsa, Skyrim - Meshes0.bsa\n");

        BSABuilder misc;
        misc.addFile("meshes\\shared.nif", "misc");
        misc.addFile("meshes\\misc_only.nif", "misc only");
        misc.write(m_game.dataDir() / L"Skyrim - Misc.bsa");

        BSABuilder modA;
        modA.addFile("meshes\\shared.nif", "moda");
        modA.addFile("meshes\\moda_only.nif", "moda only");
        modA.addFile("meshes\\loose.nif", "archived version");
        modA.addFile("meshes\\Sub\\Mixed Case.NIF", "mixed case");
        modA.addFile("textures\\t.dds", "texture");
        modA.addFile("scripts\\s.pex", "script");
        modA.addFile("meshes\\plugin.esp", "not an esp");
        modA.addFile("textures\\caf\xE9.dds", "non ascii");
        modA.write(m_game.dataDir() / L"ModA.bsa");

        BSABuilder modATextures;
        modATextures.addFile("textures\\a_tex.dds", "a texture");
        modATextures.write(m_game.dataDir() / L"ModA - Textures.bsa");

        BSABuilder modB;
        modB.addFile("meshes\\shared.nif", "modb");
        modB.write(m_game.dataDir() / L"ModB.bsa");

        BSABuilder unloaded;
        unloaded.addFile("meshes\\unloaded.nif", "unloaded");
        unloaded.write(m_game.dataDir() / L"Unloaded.bsa");

        // "ModA" followed by a superscript two: not one of the numbered archives of ModA.esp.
        BSABuilder superscript;
        superscript.addFile("meshes\\superscript.nif", "superscript");
        superscript.write(m_game.dataDir() / L"ModA².bsa");

        m_game.writeDataFile(L"meshes\\loose.nif", "loose version");
        m_game.writeDataFile(L"meshes\\loose_only.nif", "loose only");
    }

    [[nodiscard]] FakeGame& fake() { return m_game; }
};

class RecordingObserver : public BethesdaDirectory::FileQueryObserver {
public:
    struct IsFileCall {
        std::wstring path;
        bool exists;
        bool isGenerated;
    };

    std::vector<IsFileCall> isFileCalls;
    std::vector<std::pair<std::wstring, BethesdaDirectory::FileIdentity>> getFileCalls;

    void onIsFile(const std::filesystem::path& relPath,
                  bool exists,
                  bool isGenerated) override
    {
        isFileCalls.push_back({ .path = relPath.wstring(), .exists = exists, .isGenerated = isGenerated });
    }

    void onGetFile(const std::filesystem::path& relPath,
                   const BethesdaDirectory::FileIdentity& identity) override
    {
        getFileCalls.emplace_back(relPath.wstring(), identity);
    }
};

TEST(BethesdaDirectoryTest,
     ConstructorRejectsANullGame)
{
    EXPECT_THROW(BethesdaDirectory(nullptr, { L"meshes" }, ""), std::invalid_argument);
}

TEST(BethesdaDirectoryTest,
     QueriesThrowUntilTheMapIsPopulated)
{
    const TempDir dir;
    BethesdaDirectory bd(dir.path(), { L"meshes" }, dir / L"out");

    EXPECT_THROW(std::ignore = bd.isFile(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.isLooseFile(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.isBSAFile(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.isGenerated(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.looseFileFullPath(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.isFileInBSA(L"meshes\\a.nif", { }), std::runtime_error);
    EXPECT_THROW(std::ignore = bd.file(L"meshes\\a.nif"), std::runtime_error);
    EXPECT_EQ(bd.fileIdentity(L"meshes\\a.nif").kind, BethesdaDirectory::FileIdentity::Kind::None);
    EXPECT_TRUE(bd.fileMap().empty());

    // Without a game there is no archive load order.
    EXPECT_THROW(std::ignore = bd.bsaLoadOrder(), std::runtime_error);
}

TEST(BethesdaDirectoryTest,
     PlainFolderMapsTheTopLevelAndTheRequestedFoldersOnly)
{
    const TempDir dir;
    const auto data = dir / L"Data";
    TestUtil::writeFile(data / L"meshes\\a.nif", "a");
    TestUtil::writeFile(data / L"meshes\\sub\\B.NIF", "b");
    TestUtil::writeFile(data / L"Textures\\t.dds", "t");
    TestUtil::writeFile(data / L"top.txt", "top");
    TestUtil::writeFile(data / L"plugin.esp", "esp");
    TestUtil::writeFile(data / L"archive.bsa", "bsa");
    TestUtil::writeFile(data / L"other.esm", "esm");
    TestUtil::writeFile(data / L"light.esl", "esl");
    TestUtil::writeFile(data / L"scripts\\s.pex", "pex");
    TestUtil::writeFile(data / L"meshes\\.hidden\\h.nif", "hidden folder");
    TestUtil::writeFile(data / L"meshes\\.dotfile.nif", "dotfile");
    TestUtil::writeFile(data / L"meshes\\mo.nif.mohidden", "mo hidden");
    TestUtil::writeFile(data / L"meshes\\attr.nif", "hidden attribute");
    ASSERT_TRUE(SetFileAttributesW((data / L"meshes\\attr.nif").c_str(), FILE_ATTRIBUTE_HIDDEN));
    TestUtil::writeFile(data / L"meshes\\plugin_in_folder.esp", "esp in folder");

    BethesdaDirectory bd(data, { L"meshes", L"textures" }, dir / L"out");
    EXPECT_EQ(bd.dataPath(), data);
    EXPECT_EQ(bd.generatedPath(), dir / L"out");

    bd.populateFileMap(true, true);
    EXPECT_EQ(mappedPaths(bd),
              (std::set<std::wstring> { L"meshes\\a.nif", L"meshes\\sub\\b.nif", L"textures\\t.dds", L"top.txt" }));

    EXPECT_TRUE(bd.isFile(L"meshes\\sub\\b.nif"));
    EXPECT_FALSE(bd.isFile(L"meshes\\sub\\B.NIF")); // lookups take lower case paths
    EXPECT_FALSE(bd.isFile(L"scripts\\s.pex"));
    EXPECT_TRUE(bd.isLooseFile(L"meshes\\a.nif"));
    EXPECT_FALSE(bd.isBSAFile(L"meshes\\a.nif"));
    EXPECT_FALSE(bd.isGenerated(L"meshes\\a.nif"));
    EXPECT_FALSE(bd.isLooseFile(L"meshes\\missing.nif"));
    EXPECT_FALSE(bd.isFileInBSA(L"meshes\\a.nif", { L"anything.bsa" }));
    EXPECT_EQ(bd.looseFileFullPath(L"meshes\\a.nif"), data / L"meshes\\a.nif");
    EXPECT_EQ(bd.looseFileFullPath(L"meshes\\missing.nif"), data / L"meshes\\missing.nif");
    EXPECT_EQ(bd.modLookupFile(L"meshes\\a.nif"), L"meshes\\a.nif");
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\sub\\b.nif")), "b");
    EXPECT_THROW(std::ignore = bd.file(L"meshes\\missing.nif"), std::runtime_error);

    const auto identity = bd.fileIdentity(L"meshes\\a.nif");
    EXPECT_EQ(identity.kind, BethesdaDirectory::FileIdentity::Kind::Loose);
    EXPECT_EQ(identity.size, 1U);
    EXPECT_EQ(identity.mtime, std::filesystem::last_write_time(data / L"meshes\\a.nif").time_since_epoch().count());
    EXPECT_TRUE(identity.bsaRelPath.empty());
    EXPECT_EQ(bd.fileIdentity(L"meshes\\missing.nif").kind, BethesdaDirectory::FileIdentity::Kind::None);

    // Populating again rebuilds the map instead of adding to it.
    std::filesystem::remove(data / L"top.txt");
    bd.populateFileMap(false, false);
    EXPECT_EQ(mappedPaths(bd),
              (std::set<std::wstring> { L"meshes\\a.nif", L"meshes\\sub\\b.nif", L"textures\\t.dds" }));
}

TEST(BethesdaDirectoryTest,
     ArchivesAreMappedInLoadOrderBehindLooseFiles)
{
    for (const bool multithread : { false, true }) {
        ArchivedGame archived;
        BethesdaGame game = archived.fake().game();
        BethesdaDirectory bd(&game, { L"meshes", L"textures" }, archived.fake().gameDir() / L"out");

        const std::vector<std::wstring> expectedOrder {
            L"Skyrim - Misc.bsa", L"Skyrim - Meshes0.bsa", L"ModA.bsa", L"ModA - Textures.bsa", L"ModB.bsa",
        };
        EXPECT_EQ(bd.bsaLoadOrder(), expectedOrder);

        bd.populateFileMap(true, multithread);

        EXPECT_EQ(mappedPaths(bd),
                  (std::set<std::wstring> { L"meshes\\shared.nif",
                                            L"meshes\\misc_only.nif",
                                            L"meshes\\moda_only.nif",
                                            L"meshes\\loose.nif",
                                            L"meshes\\loose_only.nif",
                                            L"meshes\\sub\\mixed case.nif",
                                            L"textures\\t.dds",
                                            L"textures\\a_tex.dds" }));

        // The last archive wins, a loose file beats every archive.
        EXPECT_EQ(bytesToString(bd.file(L"meshes\\shared.nif")), "modb");
        EXPECT_EQ(bytesToString(bd.file(L"meshes\\loose.nif")), "loose version");
        EXPECT_EQ(bytesToString(bd.file(L"meshes\\misc_only.nif")), "misc only");
        EXPECT_EQ(bytesToString(bd.file(L"meshes\\sub\\mixed case.nif")), "mixed case");
        EXPECT_EQ(bytesToString(bd.file(L"textures\\a_tex.dds")), "a texture");

        EXPECT_TRUE(bd.isBSAFile(L"meshes\\shared.nif"));
        EXPECT_FALSE(bd.isLooseFile(L"meshes\\shared.nif"));
        EXPECT_TRUE(bd.isLooseFile(L"meshes\\loose.nif"));
        EXPECT_FALSE(bd.isBSAFile(L"meshes\\loose.nif"));
        EXPECT_TRUE(bd.isFileInBSA(L"meshes\\shared.nif", { L"modb.bsa" }));
        EXPECT_TRUE(bd.isFileInBSA(L"meshes\\shared.nif", { L"other.bsa", L"MODB.BSA" }));
        EXPECT_FALSE(bd.isFileInBSA(L"meshes\\shared.nif", { L"ModA.bsa" }));
        EXPECT_FALSE(bd.isFileInBSA(L"meshes\\loose.nif", { L"ModA.bsa" }));
        EXPECT_EQ(bd.modLookupFile(L"meshes\\shared.nif"), L"modb.bsa");
        EXPECT_EQ(bd.modLookupFile(L"meshes\\loose.nif"), L"meshes\\loose.nif");
        EXPECT_EQ(bd.modLookupFile(L"meshes\\missing.nif"), L"meshes\\missing.nif");

        const auto identity = bd.fileIdentity(L"meshes\\shared.nif");
        EXPECT_EQ(identity.kind, BethesdaDirectory::FileIdentity::Kind::BSA);
        EXPECT_EQ(identity.bsaRelPath, L"modb.bsa");
        const auto archivePath = archived.fake().dataDir() / L"ModB.bsa";
        EXPECT_EQ(identity.bsaSize, std::filesystem::file_size(archivePath));
        EXPECT_EQ(identity.bsaMtime, std::filesystem::last_write_time(archivePath).time_since_epoch().count());
        EXPECT_EQ(identity.mtime, 0);
        EXPECT_EQ(identity.size, 0U);

        // Archived files of unmapped folders, blocked extensions and non-ASCII names are left out.
        EXPECT_FALSE(bd.isFile(L"scripts\\s.pex"));
        EXPECT_FALSE(bd.isFile(L"meshes\\plugin.esp"));
        EXPECT_FALSE(bd.isFile(L"meshes\\unloaded.nif"));
        EXPECT_FALSE(bd.isFile(L"meshes\\superscript.nif"));
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "Unloaded.bsa not loaded by any active plugin or INI"));
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "BSA is in INI but does not exist"));
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "contains non-ascii characters"));
    }
}

TEST(BethesdaDirectoryTest,
     GeneratedFilesShadowTheLoadOrderUntilCleared)
{
    ArchivedGame archived;
    BethesdaGame game = archived.fake().game();
    const auto out = archived.fake().gameDir() / L"out";
    BethesdaDirectory bd(&game, { L"meshes" }, out);
    bd.populateFileMap(true, false);

    TestUtil::writeFile(out / L"meshes\\shared.nif", "generated");
    TestUtil::writeFile(out / L"meshes\\new.nif", "brand new");
    bd.addGeneratedFile(L"meshes\\shared.nif");
    bd.addGeneratedFile(L"meshes\\new.nif");

    EXPECT_TRUE(bd.isGenerated(L"meshes\\shared.nif"));
    EXPECT_TRUE(bd.isLooseFile(L"meshes\\shared.nif")); // generated files count as loose
    EXPECT_FALSE(bd.isBSAFile(L"meshes\\shared.nif"));
    EXPECT_EQ(bd.looseFileFullPath(L"meshes\\shared.nif"), out / L"meshes\\shared.nif");
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\shared.nif")), "generated");
    EXPECT_EQ(bd.fileIdentity(L"meshes\\shared.nif").kind, BethesdaDirectory::FileIdentity::Kind::Generated);
    EXPECT_EQ(bd.modLookupFile(L"meshes\\shared.nif"), L"meshes\\shared.nif");
    EXPECT_TRUE(bd.isFile(L"meshes\\new.nif"));
    EXPECT_TRUE(bd.isGenerated(L"meshes\\new.nif"));
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\new.nif")), "brand new");
    EXPECT_FALSE(bd.isGenerated(L"meshes\\loose.nif"));

    // Registering the same generated file twice keeps the original for restoring.
    bd.addGeneratedFile(L"meshes\\shared.nif");

    bd.clearGeneratedFiles();
    EXPECT_FALSE(bd.isGenerated(L"meshes\\shared.nif"));
    EXPECT_TRUE(bd.isBSAFile(L"meshes\\shared.nif"));
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\shared.nif")), "modb");
    EXPECT_FALSE(bd.isFile(L"meshes\\new.nif"));
    EXPECT_EQ(bd.fileIdentity(L"meshes\\new.nif").kind, BethesdaDirectory::FileIdentity::Kind::None);
}

TEST(BethesdaDirectoryTest,
     FileQueryObserverIsPerThread)
{
    ArchivedGame archived;
    BethesdaGame game = archived.fake().game();
    const auto out = archived.fake().gameDir() / L"out";
    BethesdaDirectory bd(&game, { L"meshes" }, out);
    bd.populateFileMap(true, false);
    TestUtil::writeFile(out / L"meshes\\gen.nif", "generated");
    bd.addGeneratedFile(L"meshes\\gen.nif");

    RecordingObserver observer;
    BethesdaDirectory::setThreadFileQueryObserver(&observer);
    EXPECT_TRUE(bd.isFile(L"meshes\\shared.nif"));
    EXPECT_FALSE(bd.isFile(L"meshes\\missing.nif"));
    EXPECT_TRUE(bd.isFile(L"meshes\\gen.nif"));
    std::ignore = bd.file(L"meshes\\loose.nif");
    EXPECT_THROW(std::ignore = bd.file(L"meshes\\missing.nif"), std::runtime_error);

    // Other threads have their own (unset) observer.
    std::thread([&bd] {
        std::ignore = bd.isFile(L"meshes\\shared.nif");
        std::ignore = bd.file(L"meshes\\shared.nif");
    }).join();

    BethesdaDirectory::setThreadFileQueryObserver(nullptr);
    std::ignore = bd.isFile(L"meshes\\shared.nif");
    std::ignore = bd.file(L"meshes\\shared.nif");

    ASSERT_EQ(observer.isFileCalls.size(), 3U);
    EXPECT_EQ(observer.isFileCalls[0].path, L"meshes\\shared.nif");
    EXPECT_TRUE(observer.isFileCalls[0].exists);
    EXPECT_FALSE(observer.isFileCalls[0].isGenerated);
    EXPECT_EQ(observer.isFileCalls[1].path, L"meshes\\missing.nif");
    EXPECT_FALSE(observer.isFileCalls[1].exists);
    EXPECT_FALSE(observer.isFileCalls[1].isGenerated);
    EXPECT_TRUE(observer.isFileCalls[2].exists);
    EXPECT_TRUE(observer.isFileCalls[2].isGenerated);

    ASSERT_EQ(observer.getFileCalls.size(), 1U); // a failed read is not reported
    EXPECT_EQ(observer.getFileCalls[0].first, L"meshes\\loose.nif");
    EXPECT_EQ(observer.getFileCalls[0].second.kind, BethesdaDirectory::FileIdentity::Kind::Loose);
    EXPECT_EQ(observer.getFileCalls[0].second, bd.fileIdentity(L"meshes\\loose.nif"));
}

TEST(BethesdaDirectoryTest,
     PluginArchivesFollowTheGamesNamingRules)
{
    FakeGame fake;
    fake.addPlugin(L"Mod.esp");
    for (const auto* name : {
             L"Mod.bsa",
             L"Mod0.bsa",
             L"mod1.bsa",
             L"Mod - Textures.bsa",
             L"Mod Extra.bsa",
             L"Modx.bsa",
             L"ModernArmor.bsa",
             L"Other.bsa",
         }) {
        BSABuilder builder;
        builder.addFile("meshes\\a.nif", "a");
        builder.write(fake.dataDir() / name);
    }

    BethesdaGame game = fake.game();
    const BethesdaDirectory bd(&game, { L"meshes" }, fake.gameDir() / L"out");
    const auto order = bd.bsaLoadOrder();
    ASSERT_EQ(order.size(), 4U);
    EXPECT_EQ(order[0], L"Mod.bsa"); // the archive named after the plugin loads first
    EXPECT_EQ(std::set<std::wstring>(order.begin() + 1, order.end()),
              (std::set<std::wstring> { L"Mod0.bsa", L"mod1.bsa", L"Mod - Textures.bsa" }));

    for (const auto* unloaded : { "Mod Extra.bsa", "Modx.bsa", "ModernArmor.bsa", "Other.bsa" })
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, std::string(unloaded) + " not loaded by any active plugin"));
}

TEST(BethesdaDirectoryTest,
     IniArchivesComeFirstAndLaterInisOverrideEarlierOnes)
{
    FakeGame fake;
    fake.addPlugin(L"Mod.esp");
    BSABuilder builder;
    builder.addFile("meshes\\a.nif", "a");
    builder.write(fake.dataDir() / L"Mod.bsa");

    fake.writeMainINI("; comment\n[Display]\nsResourceArchiveList=Wrong.bsa\n\n[Archive]\n"
                      "sResourceArchiveList=A.bsa, B.bsa\nsResourceArchiveList2=C.bsa\n# another comment\n");
    fake.writeCustomINI("[ARCHIVE]\nSResourceArchiveList2 = D.bsa , A.bsa\n");

    BethesdaGame game = fake.game();
    const BethesdaDirectory bd(&game, { L"meshes" }, fake.gameDir() / L"out");
    EXPECT_EQ(bd.bsaLoadOrder(), (std::vector<std::wstring> { L"A.bsa", L"B.bsa", L"D.bsa", L"Mod.bsa" }));

    // An INI in the data folder overrides the game's INIs key by key, and an archive is only listed once.
    fake.writeDataFile(L"override.ini", "[Archive]\nsResourceArchiveList=E.bsa\nsResourceArchiveListBeta=E.bsa\n");
    EXPECT_EQ(bd.bsaLoadOrder(), (std::vector<std::wstring> { L"E.bsa", L"D.bsa", L"A.bsa", L"Mod.bsa" }));
}

TEST(BethesdaDirectoryTest,
     CheckGlobUsesWildcards)
{
    EXPECT_TRUE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { L"*.nif" }));
    EXPECT_TRUE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { L"meshes\\*" }));
    EXPECT_TRUE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { L"*\\armor\\*" }));
    EXPECT_TRUE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { L"*.dds", L"meshes\\armor\\iro?.nif" }));
    EXPECT_FALSE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { L"*.dds" }));
    EXPECT_FALSE(BethesdaDirectory::checkGlob(L"meshes\\armor\\iron.nif", { }));
    EXPECT_FALSE(BethesdaDirectory::checkGlob(L"", { L"*.nif" }));
}

TEST(BethesdaDirectoryTest,
     IsHiddenCoversAttributesDotfilesAndMoHidden)
{
    FileUtil::DirectoryEntry entry;
    entry.name = L"normal.nif";
    EXPECT_FALSE(BethesdaDirectory::isHidden(entry));

    entry.isHidden = true;
    EXPECT_TRUE(BethesdaDirectory::isHidden(entry));
    entry.isHidden = false;

    entry.name = L".git";
    EXPECT_TRUE(BethesdaDirectory::isHidden(entry));

    entry.name = L"mesh.nif.mohidden";
    EXPECT_TRUE(BethesdaDirectory::isHidden(entry));
    entry.name = L"mesh.nif.MOHIDDEN";
    EXPECT_TRUE(BethesdaDirectory::isHidden(entry));

    entry.name = L"mohidden.nif";
    EXPECT_FALSE(BethesdaDirectory::isHidden(entry));
}

TEST(BethesdaDirectoryTest,
     IsPathAscii)
{
    EXPECT_TRUE(BethesdaDirectory::isPathAscii(L"meshes\\armor\\iron.nif"));
    EXPECT_TRUE(BethesdaDirectory::isPathAscii(L""));
    EXPECT_FALSE(BethesdaDirectory::isPathAscii(L"meshes\\über.nif"));
}

TEST(BethesdaDirectoryTest,
     FileIdentityEquality)
{
    using Identity = BethesdaDirectory::FileIdentity;
    const Identity none;
    EXPECT_EQ(none, Identity { });

    Identity loose;
    loose.kind = Identity::Kind::Loose;
    loose.mtime = 5;
    loose.size = 7;
    Identity sameLoose = loose;
    EXPECT_EQ(loose, sameLoose);
    sameLoose.size = 8;
    EXPECT_NE(loose, sameLoose);

    Identity bsa;
    bsa.kind = Identity::Kind::BSA;
    bsa.bsaRelPath = L"a.bsa";
    Identity otherBSA = bsa;
    otherBSA.bsaMtime = 1;
    EXPECT_NE(bsa, otherBSA);
    EXPECT_NE(bsa, loose);
}

TEST(BethesdaDirectoryTest,
     FileReaderServesEveryMappedFile)
{
    FakeGame fake;
    fake.writeDataFile(L"meshes\\loose.nif", "loose");
    BethesdaDirectory bd(fake.dataDir(), { L"meshes" }, fake.dataDir() / L"out");
    bd.populateFileMap(false, false);
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\loose.nif")), "loose");

    std::vector<std::filesystem::path> requested;
    bd.setFileReader([&requested](const std::filesystem::path& relPath) {
        requested.push_back(relPath);
        return std::vector<std::byte> { std::byte { 'r' } };
    });
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\loose.nif")), "r");
    EXPECT_EQ(requested, (std::vector<std::filesystem::path> { L"meshes\\loose.nif" }));

    // Only mapped files are read through the reader.
    EXPECT_THROW(std::ignore = bd.file(L"meshes\\missing.nif"), std::runtime_error);
    EXPECT_EQ(requested.size(), 1U);

    bd.setFileReader({ });
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\loose.nif")), "loose");
}

/// @brief A source that hands out a fixed list of loose files.
class VectorLooseFileSource : public BethesdaDirectory::LooseFileSource {
public:
    std::vector<BethesdaDirectory::LooseFile> files;

    void forEachLooseFile(const std::function<void(const BethesdaDirectory::LooseFile&)>& visitor) const override
    {
        for (const auto& file : files)
            visitor(file);
    }
};

TEST(BethesdaDirectoryTest,
     LooseFileSourceReplacesTheListing)
{
    const TempDir dir;
    const auto data = dir / L"Data";
    std::filesystem::create_directories(data);
    TestUtil::writeFile(data / L"meshes\\listed.nif", "listed");

    VectorLooseFileSource source;
    source.files = {
        { .relPath = L"Meshes\\A.nif", .mtime = 10, .size = 5, .isHidden = false, .isInHiddenFolder = false },
        { .relPath = L"textures\\t.dds", .mtime = 20, .size = 6, .isHidden = false, .isInHiddenFolder = false },
        { .relPath = L"scripts\\s.pex", .mtime = 30, .size = 7, .isHidden = false, .isInHiddenFolder = false },
        { .relPath = L"top.txt", .mtime = 40, .size = 8, .isHidden = false, .isInHiddenFolder = false },
        { .relPath = L"plugin.esp", .mtime = 50, .size = 9, .isHidden = false, .isInHiddenFolder = false },
        { .relPath = L"meshes\\h.nif", .mtime = 60, .size = 10, .isHidden = true, .isInHiddenFolder = false },
        { .relPath = L"meshes\\.d\\x.nif", .mtime = 70, .size = 11, .isHidden = false, .isInHiddenFolder = true },
        { .relPath = L"meshes\\ex.nif", .mtime = 80, .size = 12, .isHidden = false, .isInHiddenFolder = false },
    };

    BethesdaDirectory bd(data, { L"meshes", L"textures" }, dir / L"out");
    bd.setExcludedFiles({ L"meshes\\ex.nif" });
    bd.setLooseFileSource(&source);
    bd.populateFileMap(false, false);
    EXPECT_EQ(mappedPaths(bd), (std::set<std::wstring> { L"meshes\\a.nif", L"textures\\t.dds", L"top.txt" }));

    const auto identity = bd.fileIdentity(L"meshes\\a.nif");
    EXPECT_EQ(identity.kind, BethesdaDirectory::FileIdentity::Kind::Loose);
    EXPECT_EQ(identity.mtime, 10);
    EXPECT_EQ(identity.size, 5U);
    EXPECT_TRUE(bd.isLooseFile(L"textures\\t.dds"));

    // Hidden files are the source's to flag and the directory's to keep or skip.
    bd.setShouldSkipHiddenFiles(false);
    bd.populateFileMap(false, false);
    EXPECT_EQ(mappedPaths(bd),
              (std::set<std::wstring> {
                  L"meshes\\a.nif", L"textures\\t.dds", L"top.txt", L"meshes\\h.nif", L"meshes\\.d\\x.nif" }));

    // Without the source the data folder is listed again.
    bd.setLooseFileSource(nullptr);
    bd.populateFileMap(false, false);
    EXPECT_EQ(mappedPaths(bd), (std::set<std::wstring> { L"meshes\\listed.nif" }));
}

TEST(BethesdaDirectoryTest,
     LooseFileSourceFilesWinOverArchives)
{
    const TempDir dir;
    BSABuilder archive;
    archive.addFile("meshes\\shared.nif", "archived");
    archive.addFile("meshes\\archived.nif", "archived only");
    archive.write(dir / L"Mod.bsa");

    VectorLooseFileSource source;
    source.files = {
        { .relPath = L"meshes\\shared.nif", .mtime = 1, .size = 2, .isHidden = false, .isInHiddenFolder = false },
    };

    for (const bool shouldMultithread : { false, true }) {
        BethesdaDirectory bd(dir.path(), { L"meshes" }, dir / L"out");
        bd.setArchiveLoadOrder({ L"Mod.bsa" });
        bd.setLooseFileSource(&source);
        bd.populateFileMap(true, shouldMultithread);
        EXPECT_EQ(mappedPaths(bd), (std::set<std::wstring> { L"meshes\\shared.nif", L"meshes\\archived.nif" }));
        EXPECT_TRUE(bd.isLooseFile(L"meshes\\shared.nif"));
        EXPECT_TRUE(bd.isBSAFile(L"meshes\\archived.nif"));
        EXPECT_EQ(bd.fileIdentity(L"meshes\\shared.nif").kind, BethesdaDirectory::FileIdentity::Kind::Loose);
    }
}

TEST(BethesdaDirectoryTest,
     Mo2ViewMapsWhatAListingOfTheMergedFolderMaps)
{
    // The view of an instance against a folder merged by hand the way usvfs merges it: the same paths, sizes and
    // write times, hidden entries included and skipped alike.
    const FakeMO2 mo2;
    mo2.writeModlist("+ModA\n+ModB\n-Off\n");
    mo2.addModFile(L"ModA", L"meshes\\shared.nif", "a");
    mo2.addModFile(L"ModA", L"meshes\\Deep\\a.nif", "deep");
    mo2.addModFile(L"ModA", L"textures\\t.dds", "t");
    mo2.addModFile(L"ModA", L"meta.ini", "meta");
    mo2.addModFile(L"ModA", L"top.txt", "top");
    mo2.addModFile(L"ModA", L"Plugin.esp", "esp");
    mo2.addModFile(L"ModB", L"meshes\\shared.nif", "bb");
    mo2.addModFile(L"ModB", L"meshes\\b.nif", "b");
    mo2.addModFile(L"ModB", L"meshes\\.dot\\x.nif", "dot");
    mo2.addModFile(L"ModB", L"meshes\\hidden.nif", "hidden");
    ASSERT_TRUE(SetFileAttributesW((mo2.modsDir() / L"ModB\\meshes\\hidden.nif").c_str(), FILE_ATTRIBUTE_HIDDEN));
    mo2.addModFile(L"ModB", L"scripts\\s.pex", "pex");
    mo2.addModFile(L"Off", L"meshes\\off.nif", "off");
    mo2.addOverwriteFile(L"meshes\\shared.nif", "overwrite");
    mo2.addOverwriteFile(L"meshes\\o.nif", "o");
    mo2.addDataFile(L"meshes\\shared.nif", "data");
    mo2.addDataFile(L"meshes\\real.nif", "real");
    mo2.addDataFile(L"Skyrim.esm", "esm");

    // The merged folder: the data folder, then the mods from the last line up, then the overwrite folder, each copy
    // replacing the one before and keeping its write time.
    const TempDir mergedDir;
    const auto merged = mergedDir / L"Data";
    const auto copyInto = [&merged](const std::filesystem::path& folder) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
            if (!entry.is_regular_file())
                continue;
            const auto target = merged / std::filesystem::relative(entry.path(), folder);
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target, std::filesystem::copy_options::overwrite_existing);
            std::filesystem::last_write_time(target, std::filesystem::last_write_time(entry.path()));
            SetFileAttributesW(target.c_str(), GetFileAttributesW(entry.path().c_str()));
        }
    };
    copyInto(mo2.dataDir());
    copyInto(mo2.modsDir() / L"ModB");
    copyInto(mo2.modsDir() / L"ModA");
    copyInto(mo2.overwriteDir());

    const auto view
        = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), { .foldersToMap = { L"meshes", L"textures" } });

    for (const bool shouldSkipHidden : { true, false }) {
        BethesdaDirectory listed(merged, { L"meshes", L"textures" }, mergedDir / L"out");
        listed.setShouldSkipHiddenFiles(shouldSkipHidden);
        listed.populateFileMap(false, false);

        BethesdaDirectory viewed(mo2.dataDir(), { L"meshes", L"textures" }, mergedDir / L"out");
        viewed.setShouldSkipHiddenFiles(shouldSkipHidden);
        viewed.setLooseFileSource(&view);
        viewed.populateFileMap(false, false);

        EXPECT_EQ(mappedPaths(viewed), mappedPaths(listed)) << "skip hidden " << shouldSkipHidden;
        EXPECT_EQ(mappedPaths(viewed).count(L"meshes\\hidden.nif"), shouldSkipHidden ? 0U : 1U);
        for (const auto& [path, file] : listed.fileMap()) {
            const auto expected = listed.fileIdentity(path);
            const auto actual = viewed.fileIdentity(path);
            EXPECT_EQ(actual.kind, expected.kind) << TestUtil::pathToUTF8(path);
            EXPECT_EQ(actual.size, expected.size) << TestUtil::pathToUTF8(path);
            EXPECT_EQ(actual.mtime, expected.mtime) << TestUtil::pathToUTF8(path);
        }
    }
}

TEST(BethesdaDirectoryTest,
     InjectedArchiveOrderMapsArchivesWithoutAGame)
{
    const TempDir dir;
    BSABuilder modA;
    modA.addFile("meshes\\shared.nif", "a");
    modA.addFile("meshes\\a_only.nif", "a only");
    modA.write(dir / L"ModA.bsa");
    BSABuilder modB;
    modB.addFile("meshes\\shared.nif", "b");
    modB.write(dir / L"ModB.bsa");

    BethesdaDirectory bd(dir.path(), { L"meshes" }, dir / L"out");
    EXPECT_THROW(std::ignore = bd.bsaLoadOrder(), std::runtime_error);

    bd.setArchiveLoadOrder({ L"ModA.bsa", L"ModB.bsa" });
    EXPECT_EQ(bd.bsaLoadOrder(), (std::vector<std::wstring> { L"ModA.bsa", L"ModB.bsa" }));
    bd.populateFileMap(true, false);

    EXPECT_TRUE(bd.isBSAFile(L"meshes\\shared.nif"));
    EXPECT_TRUE(bd.isBSAFile(L"meshes\\a_only.nif"));
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\shared.nif")), "b"); // the later archive wins
    EXPECT_EQ(bd.modLookupFile(L"meshes\\shared.nif"), L"modb.bsa");

    // Without the archive contents a file is only readable through a reader; the map still knows its archive.
    bd.dropArchiveContents();
    EXPECT_THROW(std::ignore = bd.file(L"meshes\\shared.nif"), std::runtime_error);
    EXPECT_TRUE(bd.isFileInBSA(L"meshes\\shared.nif", { L"ModB.bsa" }));
    bd.setFileReader([](const std::filesystem::path& relPath) {
        const auto name = relPath.filename().string();
        std::vector<std::byte> bytes;
        for (const char ch : name)
            bytes.push_back(static_cast<std::byte>(ch));
        return bytes;
    });
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\shared.nif")), "shared.nif");
}

TEST(BethesdaDirectoryTest,
     HiddenFilesAreMappedWhenNotSkipped)
{
    FakeGame fake;
    fake.writeDataFile(L"meshes\\hidden.nif", "hidden");
    fake.writeDataFile(L"meshes\\visible.nif", "visible");
    ASSERT_TRUE(SetFileAttributesW((fake.dataDir() / L"meshes\\hidden.nif").c_str(), FILE_ATTRIBUTE_HIDDEN));

    BethesdaDirectory bd(fake.dataDir(), { L"meshes" }, fake.dataDir() / L"out");
    bd.populateFileMap(false, false);
    EXPECT_FALSE(bd.isFile(L"meshes\\hidden.nif"));
    EXPECT_TRUE(bd.isFile(L"meshes\\visible.nif"));

    bd.setShouldSkipHiddenFiles(false);
    bd.populateFileMap(false, false);
    EXPECT_TRUE(bd.isFile(L"meshes\\hidden.nif"));
    EXPECT_EQ(bytesToString(bd.file(L"meshes\\hidden.nif")), "hidden");
}

TEST(BethesdaDirectoryTest,
     FoldersToMapCanBeReplaced)
{
    FakeGame fake;
    fake.writeDataFile(L"meshes\\a.nif", "mesh");
    fake.writeDataFile(L"textures\\a.dds", "texture");

    BethesdaDirectory bd(fake.dataDir(), { L"meshes", L"textures" }, fake.dataDir() / L"out");
    bd.setFoldersToMap({ L"textures" });
    bd.populateFileMap(false, false);
    EXPECT_FALSE(bd.isFile(L"meshes\\a.nif"));
    EXPECT_TRUE(bd.isFile(L"textures\\a.dds"));
}
}
