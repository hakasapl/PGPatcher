#include "PGModManager.hpp"

#include "BSABuilder.hpp"
#include "FakeGame.hpp"
#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/ExceptionHandler.hpp"
#include "util/StringUtil.hpp"

#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <filesystem>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using ModManagerType = PGModManager::ModManagerType;
using TestUtil::TempDir;

/**
 * @brief A Mod Organizer 2 instance folder with a modorganizer.ini, a profile and a mods folder.
 */
class FakeMO2 {
private:
    TempDir m_dir;

public:
    FakeMO2()
    {
        std::filesystem::create_directories(instanceDir());
        std::filesystem::create_directories(modsDir());
        std::filesystem::create_directories(profileDir());
        writeINI("[General]\n"
                 "gameName=Skyrim Special Edition\n"
                 "game_edition=Steam\n"
                 "selected_profile=@ByteArray(Default)\n"
                 "gamePath=@ByteArray(C:\\\\Games\\\\Skyrim Special Edition)\n"
                 "\n"
                 "[Settings]\n"
                 "profiles_directory=@ByteArray(%BASE_DIR%/profiles)\n"
                 "mod_directory=@ByteArray(%BASE_DIR%/mods)\n");
    }

    [[nodiscard]] std::filesystem::path instanceDir() const { return m_dir / L"MO2"; }
    [[nodiscard]] std::filesystem::path modsDir() const { return instanceDir() / L"mods"; }
    [[nodiscard]] std::filesystem::path profileDir() const { return instanceDir() / L"profiles\\Default"; }
    [[nodiscard]] std::filesystem::path outputDir() const { return m_dir / L"Output"; }

    void writeINI(const std::string& content) const
    {
        TestUtil::writeFile(instanceDir() / L"modorganizer.ini", content);
    }

    void writeModlist(const std::string& content) const { TestUtil::writeFile(profileDir() / L"modlist.txt", content); }

    void addModFile(const std::wstring& mod,
                    const std::wstring& relPath,
                    const std::string& content = "x") const
    {
        TestUtil::writeFile(modsDir() / mod / relPath, content);
    }

    void makePortable() const { TestUtil::writeFile(instanceDir() / L"ModOrganizer.exe", "exe"); }
};

TEST(PGModManagerTest,
     TypeStringsRoundTrip)
{
    EXPECT_EQ(
        PGModManager::modManagerTypes(),
        (std::vector<ModManagerType> { ModManagerType::None, ModManagerType::Vortex, ModManagerType::ModOrganizer2 }));
    EXPECT_EQ(PGModManager::strFromModManagerType(ModManagerType::None), "None");
    EXPECT_EQ(PGModManager::strFromModManagerType(ModManagerType::Vortex), "Vortex");
    EXPECT_EQ(PGModManager::strFromModManagerType(ModManagerType::ModOrganizer2), "Mod Organizer 2");
    for (const auto type : PGModManager::modManagerTypes())
        EXPECT_EQ(PGModManager::modManagerTypeFromStr(PGModManager::strFromModManagerType(type)), type);
    EXPECT_EQ(PGModManager::modManagerTypeFromStr("MO2"), ModManagerType::None);
    EXPECT_EQ(PGModManager::modManagerTypeFromStr(""), ModManagerType::None);
}

TEST(PGModManagerTest,
     StartsEmpty)
{
    const PGModManager pgmm(ModManagerType::None);
    EXPECT_TRUE(pgmm.mods().empty());
    EXPECT_TRUE(pgmm.modsByPriority().empty());
    EXPECT_TRUE(pgmm.modsByDefaultOrder().empty());
    EXPECT_TRUE(pgmm.modFileMap().empty());
    EXPECT_EQ(pgmm.mod(L"Anything"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\a.nif"), nullptr);
    EXPECT_FALSE(pgmm.hasLoadedModRules());
    EXPECT_TRUE(pgmm.stagingLocation().empty());
}

TEST(PGModManagerTest,
     LoadJsonCreatesModsAndJsonWritesThemBack)
{
    PGModManager pgmm(ModManagerType::None);
    const nlohmann::json rules = {
        { "ModA", { { "priority", 5 }, { "enabled", true }, { "meshesignored", true } } },
        { "ModB", { { "priority", 10 }, { "enabled", false } } },
        { "ModC", nlohmann::json::object() },
        { "", { { "priority", 1 } } }, // ignored
    };
    pgmm.loadJSON(rules);
    EXPECT_TRUE(pgmm.hasLoadedModRules());
    ASSERT_EQ(pgmm.mods().size(), 3U);

    const auto modA = pgmm.mod(L"ModA");
    ASSERT_NE(modA, nullptr);
    EXPECT_EQ(modA->name, L"ModA");
    EXPECT_EQ(modA->priority, 5);
    EXPECT_TRUE(modA->isEnabled);
    EXPECT_TRUE(modA->areMeshesIgnored);
    EXPECT_FALSE(modA->isNew);

    const auto modB = pgmm.mod(L"ModB");
    ASSERT_NE(modB, nullptr);
    EXPECT_EQ(modB->priority, 10);
    EXPECT_FALSE(modB->isEnabled);
    EXPECT_FALSE(modB->areMeshesIgnored);

    const auto modC = pgmm.mod(L"ModC");
    ASSERT_NE(modC, nullptr);
    EXPECT_EQ(modC->priority, -1);
    EXPECT_FALSE(modC->isEnabled);

    // Loading again updates the existing mods.
    pgmm.loadJSON({ { "ModA", { { "priority", 7 }, { "enabled", false } } } });
    EXPECT_EQ(pgmm.mod(L"ModA"), modA);
    EXPECT_EQ(modA->priority, 7);
    EXPECT_FALSE(modA->isEnabled);
    EXPECT_FALSE(modA->areMeshesIgnored); // missing fields reset to their defaults

    const auto json = pgmm.json();
    EXPECT_EQ(json.size(), 3U);
    EXPECT_EQ(json["ModA"]["priority"], 7);
    EXPECT_EQ(json["ModA"]["enabled"], false);
    EXPECT_EQ(json["ModA"]["meshesignored"], false);
    EXPECT_EQ(json["ModB"]["priority"], 10);
    EXPECT_EQ(json["ModC"]["priority"], -1);

    // Non-ASCII names survive the round trip.
    pgmm.loadJSON({ { "Mod \xC3\xBC", { { "priority", 1 } } } });
    EXPECT_NE(pgmm.mod(L"Mod \u00FC"), nullptr);
    EXPECT_EQ(pgmm.json()["Mod \xC3\xBC"]["priority"], 1);
}

TEST(PGModManagerTest,
     LoadJsonRejectsMalformedRules)
{
    PGModManager pgmm(ModManagerType::None);
    EXPECT_THROW(pgmm.loadJSON(nlohmann::json::array()), std::runtime_error);
    EXPECT_THROW(pgmm.loadJSON({ { "ModA", 5 } }), std::runtime_error);
    EXPECT_THROW(pgmm.loadJSON({ { "ModA", { { "priority", "high" } } } }), std::runtime_error);
    EXPECT_THROW(pgmm.loadJSON({ { "ModA", { { "priority", 1.5 } } } }), std::runtime_error);
    EXPECT_THROW(pgmm.loadJSON({ { "ModA", { { "enabled", 1 } } } }), std::runtime_error);
    EXPECT_THROW(pgmm.loadJSON({ { "ModA", { { "meshesignored", "yes" } } } }), std::runtime_error);
}

TEST(PGModManagerTest,
     SortsByPriorityOrderShaderAndName)
{
    PGModManager pgmm(ModManagerType::None);
    pgmm.loadJSON({
        { "Alpha", { { "priority", 5 } } },
        { "Beta", { { "priority", 10 } } },
        { "Gamma", { { "priority", -1 } } },
        { "Delta", { { "priority", 5 } } },
    });
    pgmm.mod(L"Gamma")->shaders.insert(PGEnums::ShapeShader::TruePBR);
    pgmm.mod(L"Delta")->shaders.insert(PGEnums::ShapeShader::VanillaParallax);
    pgmm.mod(L"Alpha")->shaders.insert(PGEnums::ShapeShader::None);

    const auto names = [](const std::vector<std::shared_ptr<PGModManager::Mod>>& mods) {
        std::vector<std::wstring> out;
        for (const auto& mod : mods)
            out.push_back(mod->name);
        return out;
    };

    // Priority first; equal priorities are ordered by best shader, then name.
    EXPECT_EQ(names(pgmm.modsByPriority()), (std::vector<std::wstring> { L"Beta", L"Delta", L"Alpha", L"Gamma" }));

    // Without priorities (and equal mod manager order) the best shader wins, mods without shaders come last.
    EXPECT_EQ(names(pgmm.modsByDefaultOrder()), (std::vector<std::wstring> { L"Gamma", L"Delta", L"Alpha", L"Beta" }));

    // The mod manager order beats shaders and names.
    pgmm.mod(L"Beta")->modManagerOrder = -1;
    EXPECT_EQ(names(pgmm.modsByDefaultOrder()), (std::vector<std::wstring> { L"Beta", L"Gamma", L"Delta", L"Alpha" }));
}

TEST(PGModManagerTest,
     Mo2InstanceFilesAreRead)
{
    const FakeMO2 mo2;
    EXPECT_TRUE(PGModManager::isValidMO2InstanceDir(mo2.instanceDir()));
    EXPECT_FALSE(PGModManager::isValidMO2InstanceDir(mo2.modsDir()));
    EXPECT_FALSE(PGModManager::isValidMO2InstanceDir(mo2.instanceDir() / L"missing"));

    EXPECT_EQ(PGModManager::selectedProfileFromInstanceDir(mo2.instanceDir()), L"Default");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::SkyrimSE);
    EXPECT_EQ(PGModManager::gamePathFromInstanceDir(mo2.instanceDir()), L"C:\\Games\\Skyrim Special Edition");

    // Nothing is known about a folder without the INI.
    EXPECT_TRUE(PGModManager::selectedProfileFromInstanceDir(mo2.modsDir()).empty());
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.modsDir()), BethesdaGame::GameType::Unknown);
    EXPECT_TRUE(PGModManager::gamePathFromInstanceDir(mo2.modsDir()).empty());
}

TEST(PGModManagerTest,
     GameTypeFromInstanceDirKnowsEveryEdition)
{
    const FakeMO2 mo2;
    mo2.writeINI("[General]\ngameName=Skyrim Special Edition\ngame_edition=GOG\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::SkyrimGOG);
    mo2.writeINI("[General]\ngameName=Skyrim Special Edition\ngame_edition=Epic\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::Unknown);
    mo2.writeINI("[General]\ngameName=Skyrim VR\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::SkyrimVR);
    mo2.writeINI("[General]\ngameName=Enderal Special Edition\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::EnderalSE);
    mo2.writeINI("[General]\ngameName=Fallout 4\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::Unknown);
    mo2.writeINI("[General]\n");
    EXPECT_EQ(PGModManager::gameTypeFromInstanceDir(mo2.instanceDir()), BethesdaGame::GameType::Unknown);
}

TEST(PGModManagerTest,
     IniValuesAreUnquotedAndByteArraysDecoded)
{
    const FakeMO2 mo2;
    mo2.writeINI("[General]\nselected_profile=\"Quoted Profile\"\n"
                 "gamePath=@ByteArray(D:\\\\Mods\\\\Skyr\\xC3\\xA9m)\n");
    EXPECT_EQ(PGModManager::selectedProfileFromInstanceDir(mo2.instanceDir()), L"Quoted Profile");
    EXPECT_EQ(PGModManager::gamePathFromInstanceDir(mo2.instanceDir()), L"D:\\Mods\\Skyr\u00E9m");

    mo2.writeINI("[General]\ngamePath=@ByteArray(E:/Games/Skyrim)\n");
    EXPECT_EQ(PGModManager::gamePathFromInstanceDir(mo2.instanceDir()), std::filesystem::path(L"E:/Games/Skyrim"));
}

TEST(PGModManagerTest,
     RelativeGamePathsResolveAgainstTheMo2Folder)
{
    // Not launched from MO2, so only a portable instance knows where ModOrganizer.exe is.
    EXPECT_TRUE(PGModManager::mo2DirFromUSVFS().empty());

    const FakeMO2 mo2;
    EXPECT_TRUE(PGModManager::findMO2Dir(mo2.instanceDir()).empty());
    EXPECT_TRUE(PGModManager::findMO2Dir("").empty());
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L"Stock Game", mo2.instanceDir()), L"Stock Game"); // unresolved

    mo2.makePortable();
    EXPECT_EQ(PGModManager::findMO2Dir(mo2.instanceDir()), mo2.instanceDir());
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L"Stock Game", mo2.instanceDir()), mo2.instanceDir() / L"Stock Game");
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L"Stock Game\\", mo2.instanceDir()), mo2.instanceDir() / L"Stock Game");
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L".", mo2.instanceDir()), mo2.instanceDir());
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L"..\\Game", mo2.instanceDir()),
              mo2.instanceDir().parent_path() / L"Game");

    // Absolute and empty values are returned untouched.
    EXPECT_EQ(PGModManager::resolveMO2GamePath(L"C:\\Games\\Skyrim", mo2.instanceDir()), L"C:\\Games\\Skyrim");
    EXPECT_TRUE(PGModManager::resolveMO2GamePath(L"", mo2.instanceDir()).empty());

    mo2.writeINI("[General]\ngamePath=@ByteArray(Stock Game)\n");
    EXPECT_EQ(PGModManager::gamePathFromInstanceDir(mo2.instanceDir()), mo2.instanceDir() / L"Stock Game");
}

TEST(PGModManagerTest,
     Mo2ModlistPopulatesModsInOrder)
{
    const FakeMO2 mo2;
    mo2.writeModlist("# comment\n+ModA\n+ModB\n-Disabled\n*DLC: Dawnguard\n+Separators_separator\n\n");
    mo2.addModFile(L"ModA", L"meshes\\a.nif");
    mo2.addModFile(L"ModA", L"meshes\\Sub\\Deep.NIF");
    mo2.addModFile(L"ModA", L"textures\\a_p.dds");
    mo2.addModFile(L"ModA", L"meta.ini");
    mo2.addModFile(L"ModA", L"ModA.bsa");
    mo2.addModFile(L"ModA", L"readme.txt");
    mo2.addModFile(L"ModB", L"meshes\\a.nif"); // ModA is above ModB, so ModA keeps the file
    mo2.addModFile(L"ModB", L"meshes\\b.nif");
    mo2.addModFile(L"ModB", L"scripts\\s.pex");
    mo2.addModFile(L"ModB", L"meshes\\.hidden\\h.nif");
    mo2.addModFile(L"ModB", L"meshes\\m.nif.mohidden");
    mo2.addModFile(L"Disabled", L"meshes\\d.nif");
    mo2.addModFile(L"Separators_separator", L"meshes\\s.nif");
    std::filesystem::create_directories(mo2.outputDir());

    PGModManager pgmm(ModManagerType::ModOrganizer2);
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());

    ASSERT_EQ(pgmm.mods().size(), 2U);
    const auto modA = pgmm.mod(L"ModA");
    const auto modB = pgmm.mod(L"ModB");
    ASSERT_NE(modA, nullptr);
    ASSERT_NE(modB, nullptr);
    EXPECT_EQ(modA->modManagerOrder, 0);
    EXPECT_EQ(modB->modManagerOrder, 1);
    EXPECT_TRUE(modA->isNew);
    EXPECT_EQ(modA->priority, -1);
    EXPECT_FALSE(modA->isEnabled);
    EXPECT_EQ(modA->folder, mo2.modsDir() / L"ModA");
    EXPECT_EQ(pgmm.stagingLocation(), mo2.modsDir());
    EXPECT_EQ(pgmm.mod(L"Disabled"), nullptr);

    EXPECT_EQ(pgmm.modByFile(L"meshes\\a.nif"), modA);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\sub\\deep.nif"), modA);
    EXPECT_EQ(pgmm.modByFile(L"textures\\a_p.dds"), modA);
    EXPECT_EQ(pgmm.modByFile(L"moda.bsa"), modA);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\b.nif"), modB);
    EXPECT_EQ(pgmm.modByFile(L"meta.ini"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"readme.txt"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"scripts\\s.pex"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\.hidden\\h.nif"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\m.nif.mohidden"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\d.nif"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\s.nif"), nullptr);
    EXPECT_EQ(pgmm.modFileMap().size(), 5U);

    // Repopulating keeps the known mods and forgets the ones that left the list.
    mo2.writeModlist("+ModB\n");
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    EXPECT_EQ(pgmm.mods().size(), 1U);
    EXPECT_EQ(pgmm.mod(L"ModB"), modB);
    EXPECT_EQ(modB->modManagerOrder, 0);
    EXPECT_EQ(pgmm.mod(L"ModA"), nullptr);
}

TEST(PGModManagerTest,
     Mo2PopulateReportsBrokenSetups)
{
    const FakeMO2 mo2;
    PGModManager pgmm(ModManagerType::ModOrganizer2);

    // No modlist.txt yet.
    EXPECT_THROW(pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir()), std::runtime_error);
    // No modorganizer.ini.
    EXPECT_THROW(pgmm.populateModFileMapMO2(mo2.modsDir(), mo2.outputDir()), std::runtime_error);

    // A mod folder that does not exist is a corrupt MO2 state.
    mo2.writeModlist("+Ghost\n");
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, "modlist.txt from MO2 does not reflect the contents"));
    EXPECT_TRUE(pgmm.mods().empty());

    // The output folder must not be an enabled mod.
    mo2.writeModlist("+Output\n");
    mo2.addModFile(L"Output", L"meshes\\o.nif");
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.modsDir() / L"Output");
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, "you must disable the mod Output first"));

    // An output folder that does not exist yet is fine.
    mo2.writeModlist("+Output\n");
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir() / L"not yet created");
    EXPECT_EQ(pgmm.modByFile(L"meshes\\o.nif"), pgmm.mod(L"Output"));

    // A list without any mod.
    mo2.writeModlist("# nothing\n-Disabled\n");
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    EXPECT_TRUE(TestLog::contains(spdlog::level::critical, "modlist.txt was empty"));
}

TEST(PGModManagerTest,
     Mo2FolderSettingsAreHonored)
{
    const FakeMO2 mo2;
    const TempDir elsewhere;
    const auto profiles = elsewhere / L"MyProfiles";
    const auto mods = elsewhere / L"MyMods";
    std::string ini = "[General]\nselected_profile=@ByteArray(Custom)\n[Settings]\n";
    ini += "profiles_directory=@ByteArray(" + TestUtil::pathToUTF8(profiles) + ")\n";
    ini += "mod_directory=@ByteArray(" + TestUtil::pathToUTF8(mods) + ")\n";
    // Qt doubles the backslashes.
    for (size_t pos = ini.find('\\'); pos != std::string::npos; pos = ini.find('\\', pos + 2))
        ini.insert(pos, "\\");
    mo2.writeINI(ini);
    TestUtil::writeFile(profiles / L"Custom\\modlist.txt", "+Mod\n");
    TestUtil::writeFile(mods / L"Mod\\meshes\\m.nif", "x");

    PGModManager pgmm(ModManagerType::ModOrganizer2);
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    EXPECT_EQ(pgmm.stagingLocation(), mods);
    ASSERT_NE(pgmm.mod(L"Mod"), nullptr);
    EXPECT_EQ(pgmm.mod(L"Mod")->folder, mods / L"Mod");
    EXPECT_EQ(pgmm.modByFile(L"meshes\\m.nif"), pgmm.mod(L"Mod"));
}

TEST(PGModManagerTest,
     VortexDeploymentPopulatesMods)
{
    const TempDir dir;
    const auto data = dir / L"Data";
    const auto staging = dir / L"Staging";
    std::filesystem::create_directories(staging / L"ModA-1234-1-999");
    std::filesystem::create_directories(staging / L"ModB-5-1-1");

    nlohmann::json deployment;
    deployment["stagingPath"] = TestUtil::pathToUTF8(staging);
    deployment["files"] = nlohmann::json::array({
        { { "relPath", "meshes\\a.nif" }, { "source", "ModA-1234-1-999" } },
        { { "relPath", "Textures\\T.dds" }, { "source", "ModA-1234-1-999" } },
        { { "relPath", "scripts\\s.pex" }, { "source", "ModB-5-1-1" } }, // folder not mapped
        { { "relPath", "meshes\\missing.nif" }, { "source", "Missing-1-1-1" } }, // mod folder missing
    });
    TestUtil::writeFile(data / L"vortex.deployment.json", deployment.dump());

    PGModManager pgmm(ModManagerType::Vortex);
    pgmm.populateModFileMapVortex(data);

    ASSERT_EQ(pgmm.mods().size(), 1U);
    const auto modA = pgmm.mod(L"ModA"); // the Nexus suffix is stripped
    ASSERT_NE(modA, nullptr);
    EXPECT_TRUE(modA->isNew);
    EXPECT_EQ(modA->priority, -1);
    EXPECT_EQ(modA->modManagerOrder, 0);
    EXPECT_EQ(modA->folder, staging / L"ModA-1234-1-999");
    EXPECT_EQ(pgmm.stagingLocation(), staging);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\a.nif"), modA);
    EXPECT_EQ(pgmm.modByFile(L"textures\\t.dds"), modA);
    EXPECT_EQ(pgmm.modByFile(L"scripts\\s.pex"), nullptr);
    EXPECT_EQ(pgmm.modByFile(L"meshes\\missing.nif"), nullptr);
    EXPECT_EQ(pgmm.mod(L"ModB"), nullptr);
    EXPECT_EQ(pgmm.mod(L"Missing"), nullptr);

    // Mods that are not deployed anymore are forgotten.
    deployment["files"] = nlohmann::json::array({ { { "relPath", "meshes\\b.nif" }, { "source", "ModB-5-1-1" } } });
    TestUtil::writeFile(data / L"vortex.deployment.json", deployment.dump());
    pgmm.populateModFileMapVortex(data);
    EXPECT_EQ(pgmm.mod(L"ModA"), nullptr);
    EXPECT_NE(pgmm.mod(L"ModB"), nullptr);
}

TEST(PGModManagerTest,
     VortexPopulateRejectsBrokenManifests)
{
    const TempDir dir;
    PGModManager pgmm(ModManagerType::Vortex);
    EXPECT_THROW(pgmm.populateModFileMapVortex(dir.path()), std::runtime_error);

    TestUtil::writeFile(dir / L"vortex.deployment.json", R"({"stagingPath": "x"})");
    EXPECT_THROW(pgmm.populateModFileMapVortex(dir.path()), std::runtime_error);

    TestUtil::writeFile(dir / L"vortex.deployment.json", R"({"files": []})");
    EXPECT_THROW(pgmm.populateModFileMapVortex(dir.path()), std::runtime_error);

    TestUtil::writeFile(dir / L"vortex.deployment.json", "not json");
    EXPECT_THROW(pgmm.populateModFileMapVortex(dir.path()), std::exception);
}

TEST(PGModManagerTest,
     ModByFileSmartResolvesArchivedFilesThroughTheDirectory)
{
    const TestUtil::FakeGame fake;
    fake.addPlugin(L"Tex.esp");
    TestUtil::BSABuilder archive;
    archive.addFile("textures\\archived.dds", "DDS");
    archive.write(fake.dataDir() / L"Tex.bsa");
    fake.writeDataFile(L"meshes\\loose.nif", "x");
    BethesdaGame game = fake.game();
    PGDirectory pgd(&game, fake.gameDir() / L"out");
    pgd.populateFileMap(true, false);

    const FakeMO2 mo2;
    mo2.writeModlist("+TexMod\n+MeshMod\n");
    mo2.addModFile(L"TexMod", L"Tex.bsa");
    mo2.addModFile(L"MeshMod", L"meshes\\loose.nif");
    PGModManager pgmm(ModManagerType::ModOrganizer2);
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());

    // Without the global directory the archive cannot be resolved.
    PGGlobals::setPGD(nullptr);
    EXPECT_THROW(std::ignore = pgmm.modByFileSmart(L"textures\\archived.dds"), std::runtime_error);

    PGGlobals::setPGD(&pgd);
    EXPECT_EQ(pgmm.modByFileSmart(L"textures\\archived.dds"), pgmm.mod(L"TexMod"));
    EXPECT_EQ(pgmm.modByFileSmart(L"meshes\\loose.nif"), pgmm.mod(L"MeshMod"));
    EXPECT_EQ(pgmm.modByFileSmart(L"meshes\\unknown.nif"), nullptr);

    pgmm.addShaderToModByFile(L"textures\\archived.dds", PGEnums::ShapeShader::ComplexMaterial);
    pgmm.addShaderToModByFile(L"meshes\\unknown.nif", PGEnums::ShapeShader::TruePBR); // no mod, nothing happens
    EXPECT_EQ(pgmm.mod(L"TexMod")->shaders, (std::set<PGEnums::ShapeShader> { PGEnums::ShapeShader::ComplexMaterial }));
    EXPECT_TRUE(pgmm.mod(L"MeshMod")->shaders.empty());
    PGGlobals::setPGD(nullptr);
}

TEST(PGModManagerTest,
     UpdateStateFromModlistEnablesNewModsAndRenumbersPriorities)
{
    const FakeMO2 mo2;
    mo2.writeModlist("+Old2\n+NewPBR\n+Old1\n+Hidden\n+NewPlain\n");
    for (const auto* mod : { L"Old2", L"NewPBR", L"Old1", L"Hidden", L"NewPlain" })
        mo2.addModFile(mod, L"meshes\\" + std::wstring(mod) + L".nif");

    PGModManager pgmm(ModManagerType::ModOrganizer2);
    pgmm.loadJSON({
        { "Old1", { { "priority", 3 }, { "enabled", true } } },
        { "Old2", { { "priority", 2 }, { "enabled", true } } },
        { "Hidden", { { "priority", 7 }, { "enabled", true } } },
    });
    pgmm.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    pgmm.mod(L"Old1")->shaders = { PGEnums::ShapeShader::None, PGEnums::ShapeShader::VanillaParallax };
    pgmm.mod(L"Old2")->shaders = { PGEnums::ShapeShader::None };
    pgmm.mod(L"NewPBR")->shaders = { PGEnums::ShapeShader::TruePBR };
    pgmm.mod(L"NewPlain")->shaders = { PGEnums::ShapeShader::None };
    ASSERT_TRUE(pgmm.mod(L"NewPBR")->isNew);
    ASSERT_FALSE(pgmm.mod(L"Old1")->isNew);

    pgmm.updateStateFromModlist(false);

    // The new mod with a real shader is enabled above everything, the plain new mod stays disabled.
    EXPECT_TRUE(pgmm.mod(L"NewPBR")->isEnabled);
    EXPECT_FALSE(pgmm.mod(L"NewPlain")->isEnabled);
    EXPECT_EQ(pgmm.mod(L"NewPlain")->priority, -1);
    // Displayed mods (with shaders or meshes) get contiguous priorities in priority order: NewPBR, Old1, Old2.
    EXPECT_EQ(pgmm.mod(L"NewPBR")->priority, 4);
    EXPECT_EQ(pgmm.mod(L"Old1")->priority, 3);
    EXPECT_EQ(pgmm.mod(L"Old2")->priority, 2);
    // A mod without shaders or meshes is not displayed and keeps its priority.
    EXPECT_EQ(pgmm.mod(L"Hidden")->priority, 7);

    // With the MO2 order as the base, the modlist order decides.
    pgmm.updateStateFromModlist(true);
    EXPECT_EQ(pgmm.mod(L"Old2")->priority, 4);
    EXPECT_EQ(pgmm.mod(L"NewPBR")->priority, 3);
    EXPECT_EQ(pgmm.mod(L"Old1")->priority, 2);
    EXPECT_EQ(pgmm.mod(L"NewPlain")->priority, -1);
    EXPECT_EQ(pgmm.mod(L"Hidden")->priority, 7);
}

TEST(PGModManagerTest,
     AttributeModsGivesAnotherManagerTheInstalledOnesShadersAndMeshes)
{
    ExceptionHandler::reset();
    // The globals are reset however the test ends.
    const struct GlobalsReset {
        GlobalsReset() = default;
        ~GlobalsReset()
        {
            PGGlobals::setPGMM(nullptr);
            PGGlobals::setPGD(nullptr);
        }
        GlobalsReset(const GlobalsReset&) = delete;
        GlobalsReset& operator=(const GlobalsReset&) = delete;
        GlobalsReset(GlobalsReset&&) = delete;
        GlobalsReset& operator=(GlobalsReset&&) = delete;
    } globalsReset;

    const TestUtil::FakeGame fake;
    fake.writeDataFile(L"textures\\a\\stone.dds", "DDS");
    fake.writeDataFile(L"textures\\a\\stone_p.dds", "DDS");
    fake.writeDataFile(L"textures\\a\\caf\u00e9.dds", "DDS"); // a name outside ASCII is never mapped
    fake.writeDataFile(L"pbrnifpatcher\\x.json", "[]");
    TestUtil::NIFBuilder builder;
    PGTypes::TextureSet textures;
    textures.at(0) = L"textures\\a\\stone.dds";
    textures.at(3) = L"textures\\a\\stone_p.dds";
    builder.lightingShader(builder.addShape("Shape", textures, nifly::BSLSP_PARALLAX))->shaderFlags1
        |= nifly::SLSF1_PARALLAX;
    builder.save(fake.dataDir() / L"meshes\\a\\stone.nif");
    BethesdaGame game = fake.game();
    PGDirectory pgd(&game, fake.gameDir() / L"out");
    pgd.populateFileMap(true, false);

    const FakeMO2 mo2;
    mo2.writeModlist("+Parallax\n+Diffuse\n+PBR\n+Meshes\n+Unmapped\n");
    mo2.addModFile(L"Parallax", L"textures\\a\\stone_p.dds");
    mo2.addModFile(L"Diffuse", L"textures\\a\\stone.dds");
    mo2.addModFile(L"PBR", L"pbrnifpatcher\\x.json");
    mo2.addModFile(L"Meshes", L"meshes\\a\\stone.nif");
    mo2.addModFile(L"Unmapped", L"textures\\a\\caf\u00e9.dds");
    PGModManager installed(ModManagerType::ModOrganizer2);
    installed.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    PGGlobals::setPGD(&pgd);
    PGGlobals::setPGMM(&installed);
    pgd.mapFiles({ }, { }, { }, { }, false);
    pgd.waitForMeshMapping();
    pgd.waitForCMClassification();

    // The replay gives a second manager of the same instance exactly what the mapping gave the installed one.
    PGModManager other(ModManagerType::ModOrganizer2);
    other.populateModFileMapMO2(mo2.instanceDir(), mo2.outputDir());
    pgd.attributeMods(other);
    for (const auto* name : { L"Parallax", L"Diffuse", L"PBR", L"Meshes", L"Unmapped" }) {
        EXPECT_EQ(other.mod(name)->shaders, installed.mod(name)->shaders) << StringUtil::utf16toUTF8(name);
        EXPECT_EQ(other.mod(name)->hasMeshes, installed.mod(name)->hasMeshes) << StringUtil::utf16toUTF8(name);
    }
    EXPECT_EQ(other.mod(L"Parallax")->shaders,
              (std::set<PGEnums::ShapeShader> { PGEnums::ShapeShader::VanillaParallax }));
    EXPECT_EQ(other.mod(L"Diffuse")->shaders, (std::set<PGEnums::ShapeShader> { PGEnums::ShapeShader::None }));
    EXPECT_EQ(other.mod(L"PBR")->shaders, (std::set<PGEnums::ShapeShader> { PGEnums::ShapeShader::TruePBR }));
    EXPECT_TRUE(other.mod(L"Meshes")->hasMeshes);
    EXPECT_TRUE(other.mod(L"Meshes")->shaders.empty());
    // A texture that never reached the maps gave its mod no shader, in the installed manager and in the replay.
    EXPECT_TRUE(installed.mod(L"Unmapped")->shaders.empty());
    EXPECT_TRUE(other.mod(L"Unmapped")->shaders.empty());
}

}
