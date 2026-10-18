#include "PGPlugin.hpp"

#include "FakeGame.hpp"
#include "PGMutagenWrapper.hpp"
#include "PluginBuilder.hpp"
#include "PluginReader.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGMeshPermutationTracker.hpp"
#include "pgutil/PGTypes.hpp"
#include "util/FileUtil.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::PluginBuilder;
using TestUtil::PluginReader::objectBounds;
using TestUtil::PluginReader::readUInt32;
using TestUtil::PluginReader::signature;
using TestUtil::PluginReader::subrecord;
using TestUtil::PluginReader::subrecordString;
using FormKey = PGMeshPermutationTracker::FormKey;
using MeshResult = PGMeshPermutationTracker::MeshResult;
using ModelRecordType = PGPlugin::ModelRecordType;

constexpr uint32_t rockFormID = 0x000800;
constexpr uint32_t singlePassStaticFormID = 0x000801;
constexpr uint32_t singlePassMaterialFormID = 0x000802;
constexpr uint32_t rockTextureSetFormID = 0x000803;
constexpr uint32_t altTexStaticFormID = 0x000804;
constexpr uint32_t ironAddonFormID = 0x000805;
constexpr uint32_t multiPassMaterialFormID = 0x000806;
constexpr uint32_t multiPassStaticFormID = 0x000807;
constexpr uint32_t ignoredStaticFormID = 0x000808;
constexpr uint32_t sharedStaticFormID = 0x000809;
constexpr uint32_t oddAddonFormID = 0x00080A;
constexpr uint32_t treeFormID = 0x00080B;
constexpr uint32_t grassFormID = 0x00080C;
constexpr uint32_t activatorFormID = 0x00080D;
constexpr uint32_t boundedTreeFormID = 0x00080E;
// Records of the plugin itself: the file holds them with its own master index, the form keys use the plugin as the mod
// key and the 24 bit ID.
constexpr uint32_t newStaticFormID = 0x01000800;
constexpr uint32_t newStaticID = 0x000800;
constexpr uint32_t sharedStatic2FormID = 0x01000801;
constexpr uint32_t sharedStatic2ID = 0x000801;

constexpr PGTypes::ObjectBounds boundedTreeBounds { .min = { 1, 2, 3 }, .max = { 4, 5, 6 } };

/// @brief The flags of the TES4 header of a plugin file.
uint32_t headerFlags(const std::filesystem::path& plugin)
{
    const auto bytes = FileUtil::fileBytes(plugin);
    if (bytes.size() < 12)
        return 0;

    uint32_t flags = 0;
    std::memcpy(&flags, &bytes.at(8), sizeof(flags));
    return flags;
}

/// @brief Form ID of the first record of a type in a plugin file (the record follows its top level group header).
uint32_t firstRecordFormID(const std::filesystem::path& plugin,
                           const std::string& type)
{
    const auto bytes = FileUtil::fileBytes(plugin);
    std::string text;
    text.reserve(bytes.size());
    for (const auto byte : bytes)
        text.push_back(static_cast<char>(byte));

    // Top level groups are labelled with the record type right after "GRUP" and its size.
    size_t pos = 0;
    while ((pos = text.find("GRUP", pos)) != std::string::npos) {
        if (text.compare(pos + 8, 4, type) == 0) {
            const size_t record = pos + 24;
            if (record + 16 > text.size() || text.compare(record, 4, type) != 0)
                return 0;

            uint32_t formID = 0;
            std::memcpy(&formID, &bytes.at(record + 12), sizeof(formID));
            return formID;
        }
        pos += 4;
    }

    return 0;
}

/**
 * @brief A game with a synthetic master, a plugin overriding and adding records, a plugin with a non-ASCII name, a
 * plugin whose master is missing and an unreadable plugin.
 */
class PGPluginTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::FakeGame m_fake;
    std::optional<BethesdaGame> m_game;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    void SetUp() override
    {
        PluginBuilder master("Skyrim.esm", true);
        master.addTextureSet(
            rockTextureSetFormID, "RockTXST", { "clutter\\rock.dds", "clutter\\rock_n.dds", "", "", "", "", "", "" });
        master.addMaterialObject(singlePassMaterialFormID, "SinglePassMATO", "materials\\sp.nif", true);
        master.addMaterialObject(multiPassMaterialFormID, "MultiPassMATO", "materials\\mp.nif", false);
        master.addStatic(rockFormID, "RockSTAT", "clutter\\rock.nif");
        master.addStatic(singlePassStaticFormID, "SinglePassSTAT", "clutter\\sp.nif", { }, singlePassMaterialFormID);
        master.addStatic(multiPassStaticFormID, "MultiPassSTAT", "clutter\\mp.nif", { }, multiPassMaterialFormID);
        master.addStatic(altTexStaticFormID,
                         "AltTexSTAT",
                         "clutter\\alt.nif",
                         { { .shapeName = "Shape1", .textureSetFormID = rockTextureSetFormID, .index3D = 1 } });
        master.addStatic(
            ignoredStaticFormID, "IgnoredSTAT", "clutter\\ignored.nif", { }, 0, PluginBuilder::ignoredRecordFlag);
        master.addStatic(sharedStaticFormID, "SharedSTAT", "Clutter\\SHARED.nif");
        master.addArmorAddon(ironAddonFormID,
                             "IronAA",
                             {
                                 .maleWorld = "armor\\iron\\iron_0.nif",
                                 .femaleWorld = "armor\\iron\\ironf.nif",
                                 .maleFirstPerson = "armor\\iron\\1st_0.nif",
                                 .isMaleWeightSliderEnabled = true,
                                 .maleWorldAlternateTextures
                                 = { { .shapeName = "Body", .textureSetFormID = rockTextureSetFormID, .index3D = 0 } },
                             });
        master.addArmorAddon(
            oddAddonFormID, "OddAA", { .maleWorld = "armor\\odd.nif", .isMaleWeightSliderEnabled = true });
        master.addModelRecord("TREE", treeFormID, "TestTREE", "landscape\\tree.nif");
        master.addModelRecord("GRAS", grassFormID, "TestGRAS", "landscape\\grass.nif");
        master.addModelRecord("ACTI", activatorFormID, "TestACTI", "clutter\\activator.nif");
        master.addModelRecord("TREE", boundedTreeFormID, "BoundedTREE", "landscape\\bounded.nif", boundedTreeBounds);
        m_fake.writeDataFile(L"Skyrim.esm", master.bytes());

        PluginBuilder plugin("TestPlugin.esp", false, { "Skyrim.esm" });
        plugin.addStatic(rockFormID, "RockSTAT", "clutter\\rock2.nif"); // overrides the master's record
        plugin.addStatic(newStaticFormID, "NewSTAT", "clutter\\rock.nif");
        plugin.addStatic(sharedStatic2FormID, "SharedSTAT2", "clutter\\shared.nif");
        m_fake.addPlugin(L"TestPlugin.esp", true, plugin.bytes());

        PluginBuilder umlaut("M\xC3\xB6"
                             "d.esp",
                             false,
                             { "Skyrim.esm" });
        umlaut.addStatic(0x01000800, "UmlautSTAT", "clutter\\umlaut.nif");
        m_fake.addPlugin(L"Möd.esp", true, umlaut.bytes());

        PluginBuilder orphan("Orphan.esp", false, { "Missing.esm" });
        orphan.addStatic(0x01000800, "OrphanSTAT", "clutter\\orphan.nif");
        m_fake.addPlugin(L"Orphan.esp", true, orphan.bytes());

        std::vector<std::byte> garbage;
        for (const char ch : std::string("this is not a plugin"))
            garbage.push_back(static_cast<std::byte>(ch));
        m_fake.addPlugin(L"Broken.esp", true, garbage);

        m_game.emplace(m_fake.game());
        PGPlugin::initialize(*m_game, TestUtil::executableDir());
        PGPlugin::populateObjs();
    }

    void TearDown() override { PGPlugin::resetPatchingState(); }

    [[nodiscard]] static MeshResult resultFor(const FormKey& key,
                                              const std::wstring& meshPath,
                                              const std::unordered_map<unsigned,
                                                                       PGTypes::TextureSet>& altTex = { })
    {
        MeshResult result;
        result.meshPath = meshPath;
        result.altTexResults.emplace_back(key, altTex);
        result.idxCorrections = { { 0, 0 }, { 1, 1 } };
        return result;
    }

    /// @brief A mesh result that only carries the bounds of the mesh for one use.
    [[nodiscard]] static MeshResult boundsResultFor(const FormKey& key,
                                                    const PGTypes::ObjectBounds& bounds)
    {
        MeshResult result;
        result.altTexResults.emplace_back(key, std::unordered_map<unsigned, PGTypes::TextureSet> { });
        result.objectBounds = bounds;
        return result;
    }

    /// @brief The form key of the primary model of a record of the master.
    [[nodiscard]] static FormKey masterKey(uint32_t formID)
    {
        return { .modKey = L"Skyrim.esm", .formID = formID, .subMODL = "MODL" };
    }
};

TEST(PGPluginStaticTest,
     LanguageStringsRoundTrip)
{
    for (const auto& entry : PGPlugin::pluginLangTable) {
        EXPECT_EQ(PGPlugin::stringFromPluginLang(entry.value), entry.name);
        EXPECT_EQ(PGPlugin::pluginLangFromString(std::string(entry.name)), entry.value);
    }
    EXPECT_EQ(PGPlugin::pluginLangFromString("Klingon"), PGPlugin::PluginLang::English);
    EXPECT_EQ(PGPlugin::pluginLangFromString("english"), PGPlugin::PluginLang::English);
    EXPECT_EQ(PGPlugin::availablePluginLangStrs().size(), PGPlugin::pluginLangTable.size());
    EXPECT_EQ(PGPlugin::availablePluginLangStrs().front(), "Arabic");
}

TEST(PGPluginStaticTest,
     RecordTypeStringsRoundTrip)
{
    for (const auto& entry : PGPlugin::modelRecordTypeTable) {
        EXPECT_EQ(PGPlugin::stringFromRecType(entry.value), entry.name);
        EXPECT_EQ(PGPlugin::recTypeFromString(std::string(entry.name)), entry.value);
    }
    EXPECT_EQ(PGPlugin::recTypeFromString("NPC_"), ModelRecordType::Unknown);
    EXPECT_EQ(PGPlugin::recTypeFromString("stat"), ModelRecordType::Unknown);
    EXPECT_EQ(PGPlugin::stringFromRecType(ModelRecordType::Unknown), "");
    EXPECT_EQ(PGPlugin::availableRecTypeStrs().size(), PGPlugin::modelRecordTypeTable.size());

    // Every real record type is enabled by default.
    const auto defaults = PGPlugin::defaultRecTypeSet();
    EXPECT_EQ(defaults.size(), PGPlugin::modelRecordTypeTable.size());
    EXPECT_FALSE(defaults.contains(ModelRecordType::Unknown));
    for (const auto& entry : PGPlugin::modelRecordTypeTable)
        EXPECT_TRUE(defaults.contains(entry.value)) << entry.name;
}

TEST(PGPluginStaticTest,
     DynDOLODRecordTypesAreTheEightItScans)
{
    const auto types = PGPlugin::dynDOLODRecTypeSet();
    EXPECT_EQ(types.size(), 8U);
    for (const auto type : {
             ModelRecordType::Activator,
             ModelRecordType::Container,
             ModelRecordType::Door,
             ModelRecordType::Furniture,
             ModelRecordType::Grass,
             ModelRecordType::MoveableStatic,
             ModelRecordType::StaticObject,
             ModelRecordType::Tree,
         }) {
        EXPECT_TRUE(types.contains(type)) << PGPlugin::stringFromRecType(type);
        EXPECT_TRUE(PGPlugin::defaultRecTypeSet().contains(type)) << PGPlugin::stringFromRecType(type);
    }
    EXPECT_FALSE(types.contains(ModelRecordType::Armor));
    EXPECT_FALSE(types.contains(ModelRecordType::Unknown));
}

TEST(PGPluginStaticTest,
     PluginPathFromDataPathDropsTheMeshesOrTexturesFolder)
{
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"meshes\\clutter\\rock.nif"), L"clutter\\rock.nif");
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"textures\\clutter\\rock.dds"), L"clutter\\rock.dds");
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"meshes\\rock.nif"), L"rock.nif");
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"scripts\\x.pex"), L"scripts\\x.pex");
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"rock.nif"), L"rock.nif");
    EXPECT_EQ(PGPlugin::pluginPathFromDataPath(L"Meshes\\rock.nif"), L"Meshes\\rock.nif"); // lower case expected
    EXPECT_TRUE(PGPlugin::pluginPathFromDataPath(L"meshes").empty());
    EXPECT_TRUE(PGPlugin::pluginPathFromDataPath(L"").empty());
}

TEST_F(PGPluginTest,
       BrokenPluginsAreSkippedWithAnError)
{
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Failed to read plugin: Broken.esp"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Plugin has missing masters: Orphan.esp"));
    EXPECT_TRUE(PGPlugin::modelUses(L"meshes\\clutter\\orphan.nif").empty());
}

TEST_F(PGPluginTest,
       ANewInitializationReportsTheBrokenPluginsAgain)
{
    // The plugin library reports a warning or an error only once, until it is initialized for a new environment.
    TestLog::clear();
    PGPlugin::initialize(*m_game, TestUtil::executableDir());
    PGPlugin::populateObjs();
    EXPECT_EQ(TestLog::count(spdlog::level::err, "Failed to read plugin: Broken.esp"), 1U);
    EXPECT_EQ(TestLog::count(spdlog::level::err, "Plugin has missing masters: Orphan.esp"), 1U);
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "Weight slider enabled but mesh name doesn't end with _0.nif"));
}

TEST_F(PGPluginTest,
       ModelUsesListTheWinningRecordsUsingAMesh)
{
    // The master's record is overridden, so only the new record uses rock.nif.
    const auto rockUses = PGPlugin::modelUses(L"meshes\\clutter\\rock.nif");
    ASSERT_EQ(rockUses.size(), 1U);
    EXPECT_EQ(rockUses[0].first, (FormKey { .modKey = L"TestPlugin.esp", .formID = newStaticID, .subMODL = "MODL" }));
    EXPECT_EQ(rockUses[0].second.recType, ModelRecordType::StaticObject);
    EXPECT_FALSE(rockUses[0].second.isWeighted);
    EXPECT_FALSE(rockUses[0].second.isSinglepassMATO);
    EXPECT_FALSE(rockUses[0].second.isIgnored);
    EXPECT_FALSE(rockUses[0].second.isDummyUse);
    EXPECT_TRUE(rockUses[0].second.alternateTextures.empty());

    // The override keeps the form key of the original record.
    const auto rock2Uses = PGPlugin::modelUses(L"meshes\\clutter\\rock2.nif");
    ASSERT_EQ(rock2Uses.size(), 1U);
    EXPECT_EQ(rock2Uses[0].first, (FormKey { .modKey = L"Skyrim.esm", .formID = rockFormID, .subMODL = "MODL" }));

    // Any case matches, and uses are sorted by mod name.
    const auto sharedUses = PGPlugin::modelUses(L"MESHES\\Clutter\\SHARED.NIF");
    ASSERT_EQ(sharedUses.size(), 2U);
    EXPECT_EQ(sharedUses[0].first,
              (FormKey { .modKey = L"Skyrim.esm", .formID = sharedStaticFormID, .subMODL = "MODL" }));
    EXPECT_EQ(sharedUses[1].first,
              (FormKey { .modKey = L"TestPlugin.esp", .formID = sharedStatic2ID, .subMODL = "MODL" }));

    // Plugin names are decoded as UTF-8.
    const auto umlautUses = PGPlugin::modelUses(L"meshes\\clutter\\umlaut.nif");
    ASSERT_EQ(umlautUses.size(), 1U);
    EXPECT_EQ(umlautUses[0].first.modKey, L"Möd.esp");

    EXPECT_TRUE(PGPlugin::modelUses(L"meshes\\clutter\\nothing.nif").empty());
    EXPECT_TRUE(PGPlugin::modelUses(L"").empty());
}

TEST_F(PGPluginTest,
       ModelUsesCarryTheRecordAttributes)
{
    const auto singlePass = PGPlugin::modelUses(L"meshes\\clutter\\sp.nif");
    ASSERT_EQ(singlePass.size(), 1U);
    EXPECT_TRUE(singlePass[0].second.isSinglepassMATO);

    const auto multiPass = PGPlugin::modelUses(L"meshes\\clutter\\mp.nif");
    ASSERT_EQ(multiPass.size(), 1U);
    EXPECT_FALSE(multiPass[0].second.isSinglepassMATO);

    const auto ignored = PGPlugin::modelUses(L"meshes\\clutter\\ignored.nif");
    ASSERT_EQ(ignored.size(), 1U);
    EXPECT_TRUE(ignored[0].second.isIgnored);

    const auto altTex = PGPlugin::modelUses(L"meshes\\clutter\\alt.nif");
    ASSERT_EQ(altTex.size(), 1U);
    ASSERT_TRUE(altTex[0].second.alternateTextures.contains(1));
    const auto& textures = altTex[0].second.alternateTextures.at(1);
    EXPECT_EQ(textures[0], L"textures\\clutter\\rock.dds");
    EXPECT_EQ(textures[1], L"textures\\clutter\\rock_n.dds");
    for (size_t slot = 2; slot < numTextureSlots; slot++)
        EXPECT_TRUE(textures.at(slot).empty()) << slot;
}

TEST_F(PGPluginTest,
       WeightedArmorAddonsUseBothWeightVariants)
{
    const FormKey maleKey { .modKey = L"Skyrim.esm", .formID = ironAddonFormID, .subMODL = "MALE" };
    for (const auto* mesh : { L"meshes\\armor\\iron\\iron_0.nif", L"meshes\\armor\\iron\\iron_1.nif" }) {
        const auto uses = PGPlugin::modelUses(mesh);
        ASSERT_EQ(uses.size(), 1U) << TestUtil::pathToUTF8(mesh);
        EXPECT_EQ(uses[0].first, maleKey);
        EXPECT_TRUE(uses[0].second.isWeighted);
        EXPECT_EQ(uses[0].second.recType, ModelRecordType::ArmorAddon);
        ASSERT_TRUE(uses[0].second.alternateTextures.contains(0));
        EXPECT_EQ(uses[0].second.alternateTextures.at(0)[0], L"textures\\clutter\\rock.dds");
    }

    // The first person model shares the male weight slider setting.
    const auto firstPerson = PGPlugin::modelUses(L"meshes\\armor\\iron\\1st_1.nif");
    ASSERT_EQ(firstPerson.size(), 1U);
    EXPECT_EQ(firstPerson[0].first.subMODL, "1STMALE");
    EXPECT_TRUE(firstPerson[0].second.isWeighted);

    // The female model has no weight slider, so it is a plain use without a counterpart.
    const auto female = PGPlugin::modelUses(L"meshes\\armor\\iron\\ironf.nif");
    ASSERT_EQ(female.size(), 1U);
    EXPECT_EQ(female[0].first.subMODL, "FEMALE");
    EXPECT_FALSE(female[0].second.isWeighted);
    EXPECT_TRUE(PGPlugin::modelUses(L"meshes\\armor\\iron\\ironf_1.nif").empty());

    // A weighted model whose name does not end in _0 or _1 is reported and left out.
    EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "Weight slider enabled but mesh name doesn't end with _0.nif"));
    EXPECT_TRUE(PGPlugin::modelUses(L"meshes\\armor\\odd.nif").empty());
}

TEST_F(PGPluginTest,
       SetModelUsesWritesOverridesAndTextureSets)
{
    const FormKey newStatic { .modKey = L"TestPlugin.esp", .formID = newStaticID, .subMODL = "MODL" };
    const FormKey altTexStatic { .modKey = L"Skyrim.esm", .formID = altTexStaticFormID, .subMODL = "MODL" };
    const FormKey ironMale { .modKey = L"Skyrim.esm", .formID = ironAddonFormID, .subMODL = "MALE" };
    const FormKey dummy { .modKey = L"", .formID = 0, .subMODL = "" };

    PGTypes::TextureSet patched;
    patched[0] = L"textures\\clutter\\rock_patched.dds";
    patched[1] = L"textures\\clutter\\rock_n.dds";

    std::vector<MeshResult> results;
    results.push_back(resultFor(newStatic, L"meshes\\_pgpatcher_dups\\1\\clutter\\rock.nif"));
    results.push_back(resultFor(altTexStatic, L"meshes\\clutter\\alt.nif", { { 1, patched } }));
    results.push_back(resultFor(ironMale, L"meshes\\_pgpatcher_dups\\1\\armor\\iron\\iron_0.nif"));
    results.push_back(resultFor(dummy, L"meshes\\clutter\\dummy.nif"));
    // A use that keeps its mesh and textures changes nothing.
    results.push_back(resultFor(FormKey { .modKey = L"Skyrim.esm", .formID = rockFormID, .subMODL = "MODL" },
                                L"meshes\\clutter\\rock2.nif"));
    PGPlugin::setModelUses(results);

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    ASSERT_TRUE(std::filesystem::exists(out / L"PGPatcher.esp")); // holds the new texture set
    ASSERT_TRUE(std::filesystem::exists(out / L"PG_1.esp")); // holds the overrides
    EXPECT_FALSE(std::filesystem::exists(out / L"PG_2.esp"));
    EXPECT_EQ(headerFlags(out / L"PGPatcher.esp") & 0x1U, 0x1U);
    EXPECT_EQ(headerFlags(out / L"PG_1.esp") & 0x1U, 0U);
    EXPECT_EQ(headerFlags(out / L"PG_1.esp") & 0x200U, 0x200U); // overrides only, so a light plugin
    const auto textureSetFormID = firstRecordFormID(out / L"PGPatcher.esp", "TXST");
    EXPECT_NE(textureSetFormID, 0U);

    // The other ESM modes flag the plugins accordingly, each in a run of its own.
    const auto outAll = m_fake.gameDir() / L"OutputAll";
    std::filesystem::create_directories(outAll);
    PGPlugin::resetPatchingState();
    PGPlugin::setModelUses(results);
    PGPlugin::savePlugin(outAll, PGPlugin::ESMMode::All);
    EXPECT_EQ(headerFlags(outAll / L"PGPatcher.esp") & 0x1U, 0x1U);
    EXPECT_EQ(headerFlags(outAll / L"PG_1.esp") & 0x1U, 0x1U);
    const auto outNone = m_fake.gameDir() / L"OutputNone";
    std::filesystem::create_directories(outNone);
    PGPlugin::resetPatchingState();
    PGPlugin::setModelUses(results);
    PGPlugin::savePlugin(outNone, PGPlugin::ESMMode::None);
    EXPECT_EQ(headerFlags(outNone / L"PGPatcher.esp") & 0x1U, 0U);
    EXPECT_EQ(headerFlags(outNone / L"PG_1.esp") & 0x1U, 0U);

    // Saving the same run again writes the same plugins.
    const auto outAgain = m_fake.gameDir() / L"OutputTwice";
    std::filesystem::create_directories(outAgain);
    PGPlugin::savePlugin(outAgain, PGPlugin::ESMMode::None);
    EXPECT_EQ(FileUtil::fileBytes(outAgain / L"PG_1.esp"), FileUtil::fileBytes(outNone / L"PG_1.esp"));
    EXPECT_EQ(FileUtil::fileBytes(outAgain / L"PGPatcher.esp"), FileUtil::fileBytes(outNone / L"PGPatcher.esp"));

    // Mutagen drops a plugin that has the name of the output plugin from the load order, together with everything
    // listed behind it, so the output of a previous run is never read back through the plugin layer. The written
    // records are checked directly instead.
    EXPECT_EQ(subrecordString(out / L"PG_1.esp", "STAT", newStaticFormID, "MODL"),
              "_pgpatcher_dups\\1\\clutter\\rock.nif");
    EXPECT_EQ(subrecordString(out / L"PG_1.esp", "ARMA", ironAddonFormID, "MOD2"),
              "_pgpatcher_dups\\1\\armor\\iron\\iron_0.nif");
    EXPECT_TRUE(subrecord(out / L"PG_1.esp", "STAT", rockFormID, "MODL").empty()); // unchanged, so no override
    EXPECT_EQ(subrecordString(out / L"PGPatcher.esp", "TXST", textureSetFormID, "TX00"), "clutter\\rock_patched.dds");
    EXPECT_EQ(subrecordString(out / L"PGPatcher.esp", "TXST", textureSetFormID, "TX01"), "clutter\\rock_n.dds");

    // The alternate texture of the static points at that texture set; PGPatcher.esp is the third master of PG_1.esp.
    const auto alternateTextures = subrecord(out / L"PG_1.esp", "STAT", altTexStaticFormID, "MODS");
    ASSERT_EQ(alternateTextures.size(), 4U + 4U + 6U + 4U + 4U);
    EXPECT_EQ(readUInt32(alternateTextures, 0), 1U); // one alternate texture
    EXPECT_EQ(readUInt32(alternateTextures, 4), 6U); // the length of "Shape1"
    EXPECT_EQ(readUInt32(alternateTextures, 14), 0x02000000U | (textureSetFormID & 0x00FFFFFFU));
    EXPECT_EQ(readUInt32(alternateTextures, 18), 1U); // the 3D index

    // With the output plugins in the load order the plugin layer still reports the records as they were.
    m_fake.addPlugin(L"PGPatcher.esp", true, FileUtil::fileBytes(out / L"PGPatcher.esp"));
    m_fake.addPlugin(L"PG_1.esp", true, FileUtil::fileBytes(out / L"PG_1.esp"));
    m_game.emplace(m_fake.game());
    PGPlugin::initialize(*m_game, TestUtil::executableDir());
    PGPlugin::populateObjs();
    EXPECT_EQ(PGPlugin::modelUses(L"meshes\\clutter\\rock.nif").size(), 1U);
    EXPECT_TRUE(PGPlugin::modelUses(L"meshes\\_pgpatcher_dups\\1\\clutter\\rock.nif").empty());

    // A previous output plugin keeps the form IDs of texture sets that are created again.
    PGPlugin::populateObjs(out / L"PGPatcher.esp");
    PGPlugin::setModelUses({ resultFor(altTexStatic, L"meshes\\clutter\\alt.nif", { { 1, patched } }) });
    const auto outCarried = m_fake.gameDir() / L"OutputCarried";
    std::filesystem::create_directories(outCarried);
    PGPlugin::savePlugin(outCarried, PGPlugin::ESMMode::PGPatcherOnly);
    ASSERT_TRUE(std::filesystem::exists(outCarried / L"PGPatcher.esp"));
    EXPECT_EQ(firstRecordFormID(outCarried / L"PGPatcher.esp", "TXST"), textureSetFormID);
}

TEST_F(PGPluginTest,
       StockRecordsDescribeWhatTheOverridesReplaced)
{
    const FormKey newStatic { .modKey = L"TestPlugin.esp", .formID = newStaticID, .subMODL = "MODL" };
    const FormKey altTexStatic { .modKey = L"Skyrim.esm", .formID = altTexStaticFormID, .subMODL = "MODL" };
    const FormKey ironMale { .modKey = L"Skyrim.esm", .formID = ironAddonFormID, .subMODL = "MALE" };

    PGTypes::TextureSet patched;
    patched[0] = L"textures\\clutter\\rock_patched.dds";
    patched[1] = L"textures\\clutter\\rock_n.dds";

    std::vector<MeshResult> results;
    results.push_back(resultFor(newStatic, L"meshes\\_pgpatcher_dups\\1\\clutter\\rock.nif"));
    results.push_back(resultFor(altTexStatic, L"meshes\\clutter\\alt.nif", { { 1, patched } }));
    results.push_back(resultFor(ironMale, L"meshes\\_pgpatcher_dups\\1\\armor\\iron\\iron_0.nif"));
    // A use that keeps its mesh and textures is no stock record.
    results.push_back(resultFor(FormKey { .modKey = L"Skyrim.esm", .formID = rockFormID, .subMODL = "MODL" },
                                L"meshes\\clutter\\rock2.nif"));
    PGPlugin::setModelUses(results);

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    PGPlugin::saveStockRecords(out, { L"textures\\b\\wood_m.dds", L"textures\\a\\skin_s.dds" });
    const auto textureSetID = firstRecordFormID(out / L"PGPatcher.esp", "TXST") & 0x00FFFFFFU;
    ASSERT_NE(textureSetID, 0U);

    nlohmann::json stock;
    ASSERT_TRUE(FileUtil::getJSON(out / PGPlugin::stockRecordsFile, stock));
    EXPECT_EQ(stock["version"], 1);

    // One entry per changed record model, sorted by plugin, form ID and sub model, with the stock state and the
    // written one.
    ASSERT_EQ(stock["records"].size(), 3U) << stock.dump();
    const auto& alt = stock["records"][0];
    EXPECT_EQ(alt["plugin"], "Skyrim.esm");
    EXPECT_EQ(alt["formid"], altTexStaticFormID);
    EXPECT_EQ(alt["submodel"], "MODL");
    EXPECT_EQ(alt["stock"]["model"], "clutter\\alt.nif");
    ASSERT_EQ(alt["stock"]["alternatetextures"].size(), 1U);
    EXPECT_EQ(alt["stock"]["alternatetextures"][0]["name"], "Shape1");
    EXPECT_EQ(alt["stock"]["alternatetextures"][0]["index"], 1);
    EXPECT_EQ(alt["stock"]["alternatetextures"][0]["plugin"], "Skyrim.esm");
    EXPECT_EQ(alt["stock"]["alternatetextures"][0]["formid"], rockTextureSetFormID);
    EXPECT_EQ(alt["patched"]["model"], "clutter\\alt.nif"); // the mesh kept its path
    ASSERT_EQ(alt["patched"]["alternatetextures"].size(), 1U);
    EXPECT_EQ(alt["patched"]["alternatetextures"][0]["name"], "Shape1");
    EXPECT_EQ(alt["patched"]["alternatetextures"][0]["index"], 1);
    EXPECT_EQ(alt["patched"]["alternatetextures"][0]["plugin"], "PGPatcher.esp");
    EXPECT_EQ(alt["patched"]["alternatetextures"][0]["formid"], textureSetID);

    const auto& iron = stock["records"][1];
    EXPECT_EQ(iron["plugin"], "Skyrim.esm");
    EXPECT_EQ(iron["formid"], ironAddonFormID);
    EXPECT_EQ(iron["submodel"], "MALE");
    EXPECT_EQ(iron["stock"]["model"], "armor\\iron\\iron_0.nif");
    EXPECT_EQ(iron["patched"]["model"], "_pgpatcher_dups\\1\\armor\\iron\\iron_0.nif");
    ASSERT_EQ(iron["stock"]["alternatetextures"].size(), 1U);
    EXPECT_EQ(iron["patched"]["alternatetextures"], iron["stock"]["alternatetextures"]); // untouched entries

    const auto& rock = stock["records"][2];
    EXPECT_EQ(rock["plugin"], "TestPlugin.esp");
    EXPECT_EQ(rock["formid"], newStaticID);
    EXPECT_EQ(rock["submodel"], "MODL");
    EXPECT_EQ(rock["stock"]["model"], "clutter\\rock.nif");
    EXPECT_EQ(rock["patched"]["model"], "_pgpatcher_dups\\1\\clutter\\rock.nif");
    EXPECT_TRUE(rock["stock"]["alternatetextures"].empty());
    EXPECT_TRUE(rock["patched"]["alternatetextures"].empty());

    // The created texture set names the stock one it replaced.
    ASSERT_EQ(stock["texturesets"].size(), 1U);
    EXPECT_EQ(stock["texturesets"][0]["formid"], textureSetID);
    EXPECT_EQ(stock["texturesets"][0]["stockplugin"], "Skyrim.esm");
    EXPECT_EQ(stock["texturesets"][0]["stockformid"], rockTextureSetFormID);

    // The generated textures of the run, sorted.
    EXPECT_EQ(stock["generatedtextures"], nlohmann::json({ "textures\\a\\skin_s.dds", "textures\\b\\wood_m.dds" }));

    // Without changes the file is still written, with nothing in it: it marks the output as one PGLite accepts.
    PGPlugin::resetPatchingState();
    const auto outEmpty = m_fake.gameDir() / L"OutputEmpty";
    std::filesystem::create_directories(outEmpty);
    PGPlugin::saveStockRecords(outEmpty);
    nlohmann::json empty;
    ASSERT_TRUE(FileUtil::getJSON(outEmpty / PGPlugin::stockRecordsFile, empty));
    EXPECT_EQ(empty["version"], 1);
    EXPECT_TRUE(empty["records"].empty());
    EXPECT_TRUE(empty["texturesets"].empty());
    EXPECT_TRUE(empty["generatedtextures"].empty());
}

TEST_F(PGPluginTest,
       NothingIsWrittenWithoutChanges)
{
    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::setModelUses({ });
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_TRUE(std::filesystem::is_empty(out));

    // Changes that are reset are not written either.
    const FormKey newStatic { .modKey = L"TestPlugin.esp", .formID = newStaticID, .subMODL = "MODL" };
    PGPlugin::setModelUses({ resultFor(newStatic, L"meshes\\_pgpatcher_dups\\1\\clutter\\rock.nif") });
    PGPlugin::resetPatchingState();
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_TRUE(std::filesystem::is_empty(out));

    // The plugin state is usable again after the reset.
    PGPlugin::setModelUses({ resultFor(newStatic, L"meshes\\_pgpatcher_dups\\1\\clutter\\rock.nif") });
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_TRUE(std::filesystem::exists(out / L"PG_1.esp"));
    EXPECT_FALSE(std::filesystem::exists(out / L"PGPatcher.esp")); // no new texture set
}

TEST_F(PGPluginTest,
       SetModelUsesRejectsUnknownRecords)
{
    const FormKey unknownRecord { .modKey = L"TestPlugin.esp", .formID = 0x009999, .subMODL = "MODL" };
    EXPECT_THROW(PGPlugin::setModelUses({ resultFor(unknownRecord, L"meshes\\x.nif") }), std::runtime_error);

    const FormKey unknownSubModel { .modKey = L"TestPlugin.esp", .formID = newStaticID, .subMODL = "NOPE" };
    EXPECT_THROW(PGPlugin::setModelUses({ resultFor(unknownSubModel, L"meshes\\x.nif") }), std::runtime_error);

    const FormKey unknownPlugin { .modKey = L"Nothing.esp", .formID = 0x800, .subMODL = "MODL" };
    EXPECT_THROW(PGPlugin::setModelUses({ resultFor(unknownPlugin, L"meshes\\x.nif") }), std::runtime_error);

    // A failure is reported once; the plugin state is usable afterwards.
    EXPECT_NO_THROW(PGPlugin::setModelUses({ }));
    EXPECT_EQ(PGPlugin::modelUses(L"meshes\\clutter\\rock.nif").size(), 1U);
}

TEST_F(PGPluginTest,
       SetObjectBoundsWritesTheBoundsOfTreeAndGrassRecordsOnly)
{
    const PGTypes::ObjectBounds bounds { .min = { -10, -5, 0 }, .max = { 10, 5, 20 } };
    EXPECT_FALSE(PGPlugin::hasModifiedRecords());
    PGPlugin::setObjectBounds({
        boundsResultFor(masterKey(treeFormID), bounds),
        boundsResultFor(masterKey(grassFormID), bounds),
        boundsResultFor(masterKey(altTexStaticFormID), bounds),
        boundsResultFor(masterKey(activatorFormID), bounds),
    });
    EXPECT_TRUE(PGPlugin::hasModifiedRecords());

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    ASSERT_TRUE(std::filesystem::exists(out / L"PG_1.esp"));
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "TREE", treeFormID), bounds);
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "GRAS", grassFormID), bounds);

    // The OBND of the other types is authored differently from the geometry, so their records are not overridden.
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "STAT", altTexStaticFormID), std::nullopt);
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "ACTI", activatorFormID), std::nullopt);

    // Forgetting the run forgets the overrides.
    PGPlugin::resetPatchingState();
    EXPECT_FALSE(PGPlugin::hasModifiedRecords());
}

TEST_F(PGPluginTest,
       SetObjectBoundsSkipsUnchangedUnsupportedAndBoundlessUses)
{
    const PGTypes::ObjectBounds bounds { .min = { -10, -5, 0 }, .max = { 10, 5, 20 } };
    const FormKey dummy { .modKey = L"", .formID = 0, .subMODL = "" };
    const FormKey secondaryModel { .modKey = L"Skyrim.esm", .formID = ironAddonFormID, .subMODL = "MALE" };
    PGPlugin::setObjectBounds({
        boundsResultFor(masterKey(boundedTreeFormID), boundedTreeBounds), // the record already has these bounds
        boundsResultFor(dummy, bounds), // a use without a plugin record
        boundsResultFor(secondaryModel, bounds), // OBND belongs to the primary model only
        resultFor(masterKey(treeFormID), L"meshes\\landscape\\tree.nif"), // no bounds were computed
    });
    EXPECT_FALSE(PGPlugin::hasModifiedRecords());

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_TRUE(std::filesystem::is_empty(out));
}

TEST_F(PGPluginTest,
       SetObjectBoundsAndSetModelUsesShareTheOverriddenRecord)
{
    const PGTypes::ObjectBounds bounds { .min = { -10, -5, 0 }, .max = { 10, 5, 20 } };
    PGPlugin::setModelUses({ resultFor(masterKey(treeFormID), L"meshes\\_pgpatcher_dups\\1\\landscape\\tree.nif") });
    PGPlugin::setObjectBounds({ boundsResultFor(masterKey(treeFormID), bounds) });

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_EQ(subrecordString(out / L"PG_1.esp", "TREE", treeFormID, "MODL"),
              "_pgpatcher_dups\\1\\landscape\\tree.nif");
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "TREE", treeFormID), bounds);
}

TEST_F(PGPluginTest,
       ABoundsOnlyResultKeepsTheModelOfTheRecord)
{
    // The result the runner adds for a tree whose mesh is not output: the original mesh path and nothing else.
    const PGTypes::ObjectBounds bounds { .min = { -10, -5, 0 }, .max = { 10, 5, 20 } };
    auto result = boundsResultFor(masterKey(treeFormID), bounds);
    result.meshPath = L"meshes\\landscape\\tree.nif";
    PGPlugin::setModelUses({ result });
    PGPlugin::setObjectBounds({ result });

    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_EQ(subrecordString(out / L"PG_1.esp", "TREE", treeFormID, "MODL"), "landscape\\tree.nif");
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "TREE", treeFormID), bounds);
}

TEST_F(PGPluginTest,
       SetObjectBoundsRejectsUnknownRecords)
{
    const PGTypes::ObjectBounds bounds { .min = { -10, -5, 0 }, .max = { 10, 5, 20 } };
    const FormKey unknownRecord { .modKey = L"TestPlugin.esp", .formID = 0x009999, .subMODL = "MODL" };
    EXPECT_THROW(PGPlugin::setObjectBounds({ boundsResultFor(unknownRecord, bounds) }), std::runtime_error);

    // A failure is reported once; the plugin state is usable afterwards, which a real update shows.
    PGPlugin::setObjectBounds({ boundsResultFor(masterKey(treeFormID), bounds) });
    const auto out = m_fake.gameDir() / L"Output";
    std::filesystem::create_directories(out);
    PGPlugin::savePlugin(out, PGPlugin::ESMMode::PGPatcherOnly);
    EXPECT_EQ(objectBounds(out / L"PG_1.esp", "TREE", treeFormID), bounds);
}

TEST_F(PGPluginTest,
       WrapperReturnsRawModelUses)
{
    const auto uses = PGMutagenWrapper::libGetModelUses(L"meshes\\clutter\\alt.nif");
    ASSERT_EQ(uses.size(), 1U);
    EXPECT_EQ(uses[0].modName, L"Skyrim.esm");
    EXPECT_EQ(uses[0].formID, altTexStaticFormID);
    EXPECT_EQ(uses[0].subModel, "MODL");
    EXPECT_EQ(uses[0].type, "STAT");
    EXPECT_EQ(uses[0].meshFile, L"meshes\\clutter\\alt.nif");
    EXPECT_FALSE(uses[0].isWeighted);
    EXPECT_FALSE(uses[0].isSinglepassMATO);
    EXPECT_FALSE(uses[0].isIgnored);
    ASSERT_EQ(uses[0].alternateTextures.size(), 1U);
    EXPECT_EQ(uses[0].alternateTextures[0].slotID, 1);
    EXPECT_EQ(uses[0].alternateTextures[0].slots[0], L"textures\\clutter\\rock.dds");
    EXPECT_EQ(uses[0].alternateTextures[0].slots[7], L"");

    EXPECT_TRUE(PGMutagenWrapper::libGetModelUses(L"meshes\\none.nif").empty());

    // An empty update is accepted.
    EXPECT_NO_THROW(PGMutagenWrapper::libSetModelUses({ }));
}

}
