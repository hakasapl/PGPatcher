#include "common/MO2DataFolder.hpp"

#include "FakeMO2.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaDirectory.hpp"

#include <gtest/gtest.h>
#include <spdlog/common.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <windows.h>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::FakeMO2;
using SourceKind = MO2DataFolder::SourceKind;

MO2DataFolder::Options options(bool shouldMultithread = true)
{
    return { .foldersToMap = { L"meshes", L"textures" }, .shouldMultithread = shouldMultithread };
}

/// @brief Kind and name of the source of every file, by path.
std::map<std::wstring,
         std::pair<SourceKind,
                   std::wstring>>
filesBySource(const MO2DataFolder& view)
{
    std::map<std::wstring, std::pair<SourceKind, std::wstring>> files;
    for (const auto& file : view.files()) {
        const auto& source = view.sources().at(file.source);
        files[file.relPath] = { source.kind, source.name };
    }
    return files;
}

std::vector<std::wstring> paths(const MO2DataFolder& view)
{
    std::vector<std::wstring> paths;
    for (const auto& file : view.files())
        paths.push_back(file.relPath);
    return paths;
}

const MO2DataFolder::File& fileOf(const MO2DataFolder& view,
                                  const std::wstring& relPath)
{
    const auto it = std::ranges::find(view.files(), relPath, &MO2DataFolder::File::relPath);
    if (it == view.files().end())
        throw std::runtime_error("not in the view: " + TestUtil::pathToUTF8(relPath));
    return *it;
}

int64_t writeTime(const std::filesystem::path& path)
{
    return std::filesystem::last_write_time(path).time_since_epoch().count();
}

/// @brief The INI FakeMO2 writes, with lines added to its [Settings] section.
std::string iniWithSettings(const std::string& settings)
{
    return "[General]\n"
           "gameName=Skyrim Special Edition\n"
           "game_edition=Steam\n"
           "selected_profile=@ByteArray(Default)\n"
           "gamePath=@ByteArray(C:\\\\Games\\\\Skyrim Special Edition)\n"
           "\n"
           "[Settings]\n"
           "profiles_directory=@ByteArray(%BASE_DIR%/profiles)\n"
           "mod_directory=@ByteArray(%BASE_DIR%/mods)\n"
        + settings;
}

TEST(MO2DataFolderTest,
     ViewFollowsTheModlistTheOverwriteFolderAndTheDataFolder)
{
    const FakeMO2 mo2;
    mo2.writeModlist("# comment\n+ModA\n+ModB\n-Disabled\n*DLC: Dawnguard\n+Separators_separator\n\n");
    mo2.addModFile(L"ModA", L"meshes\\x.nif", "a");
    mo2.addModFile(L"ModA", L"meshes\\Sub\\Deep.NIF", "deep");
    mo2.addModFile(L"ModA", L"meta.ini", "a meta");
    mo2.addModFile(L"ModA", L"ModA.bsa", "bsa");
    mo2.addModFile(L"ModB", L"meshes\\x.nif", "b"); // ModA is above ModB, so ModA would win
    mo2.addModFile(L"ModB", L"meshes\\y.nif", "y");
    mo2.addModFile(L"ModB", L"textures\\t.dds", "t");
    mo2.addModFile(L"ModB", L"scripts\\s.pex", "pex"); // folder not mapped
    mo2.addModFile(L"ModB", L"meta.ini", "b meta");
    mo2.addModFile(L"Disabled", L"meshes\\d.nif", "d");
    mo2.addModFile(L"Separators_separator", L"meshes\\s.nif", "s");
    mo2.addOverwriteFile(L"meshes\\x.nif", "overwrite"); // the overwrite folder wins over every mod
    mo2.addOverwriteFile(L"meshes\\o.nif", "o");
    mo2.addDataFile(L"meshes\\x.nif", "data"); // the data folder loses to everything
    mo2.addDataFile(L"meshes\\real.nif", "real");
    mo2.addDataFile(L"Skyrim.esm", "esm");
    mo2.addDataFile(L"scripts\\real.pex", "pex");

    const auto view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_EQ(view.instanceDir(), mo2.instanceDir());
    EXPECT_EQ(view.modsDir(), mo2.modsDir());
    EXPECT_EQ(view.profile(), L"Default");

    ASSERT_EQ(view.sources().size(), 4U);
    EXPECT_EQ(view.sources()[0].kind, SourceKind::Overwrite);
    EXPECT_EQ(view.sources()[0].folder, mo2.overwriteDir());
    EXPECT_EQ(view.sources()[1].kind, SourceKind::Mod);
    EXPECT_EQ(view.sources()[1].name, L"ModA");
    EXPECT_EQ(view.sources()[1].folder, mo2.modsDir() / L"ModA");
    EXPECT_EQ(view.sources()[1].order, 0);
    EXPECT_EQ(view.sources()[2].kind, SourceKind::Mod);
    EXPECT_EQ(view.sources()[2].name, L"ModB");
    EXPECT_EQ(view.sources()[2].order, 1);
    EXPECT_EQ(view.sources()[3].kind, SourceKind::DataFolder);
    EXPECT_EQ(view.sources()[3].folder, mo2.dataDir());

    EXPECT_EQ(filesBySource(view),
              (std::map<std::wstring, std::pair<SourceKind, std::wstring>> {
                  { L"meshes\\x.nif", { SourceKind::Overwrite, L"" } },
                  { L"meshes\\o.nif", { SourceKind::Overwrite, L"" } },
                  { L"meshes\\Sub\\Deep.NIF", { SourceKind::Mod, L"ModA" } },
                  { L"meta.ini", { SourceKind::Mod, L"ModA" } },
                  { L"ModA.bsa", { SourceKind::Mod, L"ModA" } },
                  { L"meshes\\y.nif", { SourceKind::Mod, L"ModB" } },
                  { L"textures\\t.dds", { SourceKind::Mod, L"ModB" } },
                  { L"meshes\\real.nif", { SourceKind::DataFolder, L"" } },
                  { L"Skyrim.esm", { SourceKind::DataFolder, L"" } },
              }));
    EXPECT_TRUE(std::ranges::is_sorted(view.files(), { }, &MO2DataFolder::File::relPath));
    EXPECT_TRUE(view.isDataFolderListedOnDisk());

    // The owner for the mod attribution is the first mod with a copy, whatever the game sees.
    const auto ownerName = [&view](const std::wstring& relPath) {
        const auto owner = fileOf(view, relPath).ownerSource;
        return owner == MO2DataFolder::noSource ? std::wstring(L"-") : view.sources().at(owner).name;
    };
    EXPECT_EQ(ownerName(L"meshes\\x.nif"), L"ModA");
    EXPECT_EQ(ownerName(L"meshes\\o.nif"), L"-");
    EXPECT_EQ(ownerName(L"meshes\\y.nif"), L"ModB");
    EXPECT_EQ(ownerName(L"meta.ini"), L"ModA");
    EXPECT_EQ(ownerName(L"ModA.bsa"), L"ModA");
    EXPECT_EQ(ownerName(L"meshes\\real.nif"), L"-");
    EXPECT_EQ(ownerName(L"Skyrim.esm"), L"-");

    // Size and write time are the winner's.
    const auto& x = fileOf(view, L"meshes\\x.nif");
    EXPECT_EQ(x.size, 9U);
    EXPECT_EQ(x.mtime, writeTime(mo2.overwriteDir() / L"meshes\\x.nif"));
    EXPECT_FALSE(x.isHidden);
    EXPECT_FALSE(x.isInHiddenFolder);
    const auto& real = fileOf(view, L"meshes\\real.nif");
    EXPECT_EQ(real.size, 4U);
    EXPECT_EQ(real.mtime, writeTime(mo2.dataDir() / L"meshes\\real.nif"));

    // As a source of loose files, every file is visited once.
    std::size_t visited = 0;
    view.forEachLooseFile([&visited](const BethesdaDirectory::LooseFile&) { ++visited; });
    EXPECT_EQ(visited, view.files().size());

    // One thread lists the same.
    const auto single = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options(false));
    ASSERT_EQ(single.files().size(), view.files().size());
    for (std::size_t i = 0; i < view.files().size(); ++i) {
        EXPECT_EQ(single.files()[i].relPath, view.files()[i].relPath);
        EXPECT_EQ(single.files()[i].source, view.files()[i].source);
        EXPECT_EQ(single.files()[i].size, view.files()[i].size);
        EXPECT_EQ(single.files()[i].mtime, view.files()[i].mtime);
    }
}

TEST(MO2DataFolderTest,
     SkipSettingsLeaveFilesOutAsUsvfsDoes)
{
    const FakeMO2 mo2;
    std::filesystem::create_directories(mo2.dataDir());
    mo2.writeModlist("+Mod\n");
    mo2.addModFile(L"Mod", L"meshes\\m.nif.mohidden");
    mo2.addModFile(L"Mod", L"meshes\\.git\\g.nif");
    mo2.addModFile(L"Mod", L"meshes\\keep.nif");
    mo2.addModFile(L"Mod", L"meshes\\old.bak");
    mo2.addModFile(L"Mod", L"meshes\\Source\\src.nif");

    // MO2's defaults: .mohidden files and .git folders.
    auto view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_EQ(paths(view),
              (std::vector<std::wstring> { L"meshes\\Source\\src.nif", L"meshes\\keep.nif", L"meshes\\old.bak" }));

    // The lists of the instance, compared without regard to case, quoted or not.
    mo2.writeINI(iniWithSettings("skip_file_suffixes=.MOHIDDEN, .bak\nskip_directories=\".git\", source\n"));
    view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_EQ(paths(view), (std::vector<std::wstring> { L"meshes\\keep.nif" }));

    // Empty lists: nothing is skipped; the hidden file and folder are flagged.
    mo2.writeINI(iniWithSettings("skip_file_suffixes=@Invalid()\nskip_directories=@Invalid()\n"));
    view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_EQ(paths(view),
              (std::vector<std::wstring> { L"meshes\\.git\\g.nif",
                                           L"meshes\\Source\\src.nif",
                                           L"meshes\\keep.nif",
                                           L"meshes\\m.nif.mohidden",
                                           L"meshes\\old.bak" }));
    EXPECT_TRUE(fileOf(view, L"meshes\\m.nif.mohidden").isHidden);
    EXPECT_TRUE(fileOf(view, L"meshes\\.git\\g.nif").isInHiddenFolder);
    EXPECT_FALSE(fileOf(view, L"meshes\\.git\\g.nif").isHidden);
}

TEST(MO2DataFolderTest,
     HiddenFilesAndFoldersAreFlaggedNotLeftOut)
{
    const FakeMO2 mo2;
    std::filesystem::create_directories(mo2.dataDir());
    mo2.writeModlist("+Mod\n");
    mo2.addModFile(L"Mod", L"meshes\\.dot\\h.nif");
    mo2.addModFile(L"Mod", L"meshes\\.dotfile.nif");
    mo2.addModFile(L"Mod", L"meshes\\attr.nif");
    ASSERT_TRUE(SetFileAttributesW((mo2.modsDir() / L"Mod\\meshes\\attr.nif").c_str(), FILE_ATTRIBUTE_HIDDEN));
    mo2.addModFile(L"Mod", L"meshes\\hdir\\in.nif");
    mo2.addModFile(L"Mod", L"meshes\\hdir\\deeper\\in.nif");
    ASSERT_TRUE(SetFileAttributesW((mo2.modsDir() / L"Mod\\meshes\\hdir").c_str(),
                                   FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY));
    mo2.addModFile(L"Mod", L"meshes\\plain.nif");
    mo2.addDataFile(L"meshes\\.datadot\\d.nif");

    const auto view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_EQ(view.files().size(), 7U);
    EXPECT_TRUE(fileOf(view, L"meshes\\.dot\\h.nif").isInHiddenFolder);
    EXPECT_FALSE(fileOf(view, L"meshes\\.dot\\h.nif").isHidden);
    EXPECT_TRUE(fileOf(view, L"meshes\\.dotfile.nif").isHidden);
    EXPECT_TRUE(fileOf(view, L"meshes\\attr.nif").isHidden);
    EXPECT_TRUE(fileOf(view, L"meshes\\hdir\\in.nif").isInHiddenFolder);
    EXPECT_TRUE(fileOf(view, L"meshes\\hdir\\deeper\\in.nif").isInHiddenFolder);
    EXPECT_FALSE(fileOf(view, L"meshes\\plain.nif").isHidden);
    EXPECT_FALSE(fileOf(view, L"meshes\\plain.nif").isInHiddenFolder);
    EXPECT_TRUE(fileOf(view, L"meshes\\.datadot\\d.nif").isInHiddenFolder);

    // A hidden copy owns nothing below the folders to map.
    EXPECT_EQ(fileOf(view, L"meshes\\.dotfile.nif").ownerSource, MO2DataFolder::noSource);
    EXPECT_EQ(fileOf(view, L"meshes\\hdir\\in.nif").ownerSource, MO2DataFolder::noSource);
    EXPECT_NE(fileOf(view, L"meshes\\plain.nif").ownerSource, MO2DataFolder::noSource);
}

TEST(MO2DataFolderTest,
     DataFolderIsListedOnDiskThroughAJunctionOrLeftOut)
{
    const FakeMO2 mo2;
    mo2.writeModlist("+Mod\n");
    mo2.addModFile(L"Mod", L"meshes\\m.nif");
    mo2.addDataFile(L"meshes\\real.nif");
    mo2.addDataFile(L"Data.esm");

    // The game folder reached through a junction is the same folder on disk.
    const TestUtil::TempDir linkDir;
    const auto link = linkDir / L"Game";
    if (!TestUtil::createJunction(link, mo2.dataDir().parent_path()))
        GTEST_SKIP() << "junctions cannot be created here";

    const auto view = MO2DataFolder::read(mo2.instanceDir(), link / L"Data", options());
    EXPECT_TRUE(view.isDataFolderListedOnDisk());
    EXPECT_EQ(paths(view), (std::vector<std::wstring> { L"Data.esm", L"meshes\\m.nif", L"meshes\\real.nif" }));

    // A reader of the mods alone leaves the data folder out.
    auto modsOnly = options();
    modsOnly.shouldListDataFolder = false;
    const auto modsView = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), modsOnly);
    EXPECT_FALSE(modsView.isDataFolderListedOnDisk());
    EXPECT_EQ(modsView.sources().size(), 1U);
    EXPECT_EQ(paths(modsView), (std::vector<std::wstring> { L"meshes\\m.nif" }));
}

TEST(MO2DataFolderTest,
     BrokenSetupsAreReported)
{
    const FakeMO2 mo2;
    std::filesystem::create_directories(mo2.dataDir());

    // No modlist.txt yet, no modorganizer.ini.
    EXPECT_THROW(std::ignore = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options()), std::runtime_error);
    EXPECT_THROW(std::ignore = MO2DataFolder::read(mo2.modsDir(), mo2.dataDir(), options()), std::runtime_error);

    // A mod without a folder is left out: MO2 cannot serve it either.
    mo2.writeModlist("+Ghost\n+Real\n");
    mo2.addModFile(L"Real", L"meshes\\r.nif");
    const auto view = MO2DataFolder::read(mo2.instanceDir(), mo2.dataDir(), options());
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "The folder of the mod Ghost does not exist"));
    ASSERT_EQ(view.sources().size(), 2U);
    EXPECT_EQ(view.sources()[0].name, L"Real");
    EXPECT_EQ(view.sources()[0].order, 0);
    EXPECT_EQ(view.sources()[1].kind, SourceKind::DataFolder);
    EXPECT_EQ(paths(view), (std::vector<std::wstring> { L"meshes\\r.nif" }));
}

}
