#include "handlers/HandlerLightPlacerTracker.hpp"

#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;

class HandlerLightPlacerTrackerTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }
    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }

    void SetUp() override
    {
        TestUtil::writeFile(dataDir() / L"lightplacer\\a.json",
                            R"([
  {"models": ["clutter\\lamp.nif", "Armor\\IRON\\cuirass.nif"], "lights": []},
  {"lights": []},
  {"models": "not an array"},
  {"models": [1, "clutter\\candle.nif"]},
  "a string entry"
])");
        TestUtil::writeFile(dataDir() / L"lightplacer\\b.json", R"([{"models": ["clutter\\lamp.nif"]}])");
        TestUtil::writeFile(dataDir() / L"lightplacer\\object.json", R"({"models": ["clutter\\lamp.nif"]})");
        TestUtil::writeFile(dataDir() / L"lightplacer\\bom.json",
                            "\xEF\xBB\xBF[{\"models\": [\"clutter\\\\lamp.nif\"]}]");
        TestUtil::writeFile(dataDir() / L"lightplacer\\trailing.json",
                            R"([{"models": ["clutter\\lamp.nif"]}] trailing)");
        TestUtil::writeFile(dataDir() / L"lightplacer\\broken.json", "[{,");
        TestUtil::writeFile(dataDir() / L"lightplacer\\unchanged.json", R"([{"models": ["clutter\\other.nif"]}])");

        m_pgd = std::make_unique<PGDirectory>(dataDir(), outDir());
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());
    }

    void TearDown() override
    {
        HandlerLightPlacerTracker::finalize();
        PGGlobals::setPGD(nullptr);
    }

    [[nodiscard]] static std::vector<std::filesystem::path> allJSONs()
    {
        return {
            L"lightplacer\\a.json",         L"lightplacer\\b.json",        L"lightplacer\\object.json",
            L"lightplacer\\bom.json",       L"lightplacer\\trailing.json", L"lightplacer\\broken.json",
            L"lightplacer\\unchanged.json",
        };
    }
};

TEST_F(HandlerLightPlacerTrackerTest,
       InitSkipsFilesLightPlacerWouldReject)
{
    HandlerLightPlacerTracker::init(allJSONs());
    for (const auto* rejected : { "object.json", "bom.json", "broken.json" })
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, std::string("lightplacer\\") + rejected + " is invalid"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, "lightplacer\\trailing.json is invalid"));
    EXPECT_FALSE(TestLog::contains(spdlog::level::warn, "lightplacer\\a.json is invalid"));

    // Without the global directory the handler cannot read anything.
    PGGlobals::setPGD(nullptr);
    EXPECT_THROW(HandlerLightPlacerTracker::init(allJSONs()), std::runtime_error);
    // Nothing was changed, so finalizing needs no directory either.
    EXPECT_NO_THROW(HandlerLightPlacerTracker::finalize());
    PGGlobals::setPGD(m_pgd.get());
}

TEST_F(HandlerLightPlacerTrackerTest,
       DuplicatesAreAddedToEveryEntryListingTheOriginal)
{
    HandlerLightPlacerTracker::init(allJSONs());

    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\lamp.nif");
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\lamp.nif"); // again
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif", L"meshes\\clutter\\lamp.nif"); // same
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\armor\\iron\\cuirass.nif",
                                                L"meshes\\_pgpatcher_dups\\2\\armor\\iron\\cuirass.nif");
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\candle.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\candle.nif");
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\unknown.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\unknown.nif");
    HandlerLightPlacerTracker::finalize();

    const auto a = nlohmann::json::parse(TestUtil::readFile(outDir() / L"lightplacer\\a.json"));
    ASSERT_TRUE(a.is_array());
    ASSERT_EQ(a.size(), 5U);
    EXPECT_EQ(a[0]["models"],
              nlohmann::json({ "clutter\\lamp.nif",
                               "Armor\\IRON\\cuirass.nif",
                               "_pgpatcher_dups\\1\\clutter\\lamp.nif",
                               "_pgpatcher_dups\\2\\armor\\iron\\cuirass.nif" }));
    EXPECT_EQ(a[1], nlohmann::json({ { "lights", nlohmann::json::array() } }));
    EXPECT_EQ(a[2]["models"], "not an array");
    EXPECT_EQ(a[3]["models"], nlohmann::json({ 1, "clutter\\candle.nif", "_pgpatcher_dups\\1\\clutter\\candle.nif" }));
    EXPECT_EQ(a[4], "a string entry");

    const auto b = nlohmann::json::parse(TestUtil::readFile(outDir() / L"lightplacer\\b.json"));
    EXPECT_EQ(b[0]["models"], nlohmann::json({ "clutter\\lamp.nif", "_pgpatcher_dups\\1\\clutter\\lamp.nif" }));

    // The file with trailing text is accepted like Light Placer accepts it, and is written without that text.
    const auto trailing = nlohmann::json::parse(TestUtil::readFile(outDir() / L"lightplacer\\trailing.json"));
    EXPECT_EQ(trailing[0]["models"], nlohmann::json({ "clutter\\lamp.nif", "_pgpatcher_dups\\1\\clutter\\lamp.nif" }));

    // Files nothing changed are not written, and neither are the rejected ones.
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\unchanged.json"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\object.json"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\bom.json"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\broken.json"));
}

TEST_F(HandlerLightPlacerTrackerTest,
       FinalizeForgetsTheTrackedFiles)
{
    HandlerLightPlacerTracker::init(allJSONs());
    HandlerLightPlacerTracker::finalize();
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\a.json"));

    // Nothing is tracked anymore, so nothing is written.
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\lamp.nif");
    HandlerLightPlacerTracker::finalize();
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\a.json"));

    // Initializing again starts from the files on disk.
    HandlerLightPlacerTracker::init({ L"lightplacer\\b.json" });
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif",
                                                L"meshes\\_pgpatcher_dups\\3\\clutter\\lamp.nif");
    HandlerLightPlacerTracker::finalize();
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\a.json"));
    const auto b = nlohmann::json::parse(TestUtil::readFile(outDir() / L"lightplacer\\b.json"));
    EXPECT_EQ(b[0]["models"], nlohmann::json({ "clutter\\lamp.nif", "_pgpatcher_dups\\3\\clutter\\lamp.nif" }));
}

TEST_F(HandlerLightPlacerTrackerTest,
       WorksWithAnotherDirectoryAfterTheFirstRun)
{
    HandlerLightPlacerTracker::init(allJSONs());
    HandlerLightPlacerTracker::finalize();

    // A new data folder with its own global directory: the handler must not keep the old one.
    const TestUtil::TempDir other;
    TestUtil::writeFile(other / L"Data\\lightplacer\\c.json", R"([{"models": ["clutter\\lamp.nif"]}])");
    PGDirectory otherPgd(other / L"Data", other / L"Output");
    otherPgd.populateFileMap(false, false);
    PGGlobals::setPGD(&otherPgd);

    HandlerLightPlacerTracker::init({ L"lightplacer\\c.json" });
    HandlerLightPlacerTracker::handleNIFCreated(L"meshes\\clutter\\lamp.nif",
                                                L"meshes\\_pgpatcher_dups\\1\\clutter\\lamp.nif");
    HandlerLightPlacerTracker::finalize();
    EXPECT_TRUE(std::filesystem::exists(other / L"Output\\lightplacer\\c.json"));
    EXPECT_FALSE(std::filesystem::exists(outDir() / L"lightplacer\\c.json"));

    PGGlobals::setPGD(m_pgd.get());
}

TEST_F(HandlerLightPlacerTrackerTest,
       ParseLightPlacerJSONAcceptsWhatLightPlacerAccepts)
{
    // The parser is also what the PGLite runtime reads the configs with.
    nlohmann::json parsed;
    EXPECT_TRUE(HandlerLightPlacerTracker::parseLightPlacerJSON(dataDir() / L"lightplacer\\a.json", parsed));
    EXPECT_EQ(parsed.size(), 5U);
    EXPECT_TRUE(HandlerLightPlacerTracker::parseLightPlacerJSON(dataDir() / L"lightplacer\\trailing.json", parsed));
    EXPECT_EQ(parsed.size(), 1U);
    EXPECT_FALSE(HandlerLightPlacerTracker::parseLightPlacerJSON(dataDir() / L"lightplacer\\object.json", parsed));
    EXPECT_FALSE(HandlerLightPlacerTracker::parseLightPlacerJSON(dataDir() / L"lightplacer\\bom.json", parsed));
    EXPECT_FALSE(HandlerLightPlacerTracker::parseLightPlacerJSON(dataDir() / L"lightplacer\\broken.json", parsed));
}

TEST(HandlerLightPlacerModelKeyTest,
     ModelKeyIgnoresCaseAndSeparatorForm)
{
    const std::filesystem::path key = L"armor\\foo.nif";
    EXPECT_EQ(HandlerLightPlacerTracker::modelKey("Armor/Foo.NIF"), key);
    EXPECT_EQ(HandlerLightPlacerTracker::modelKey("armor//foo.nif"), key);
    EXPECT_EQ(HandlerLightPlacerTracker::modelKey("armor\\foo.nif"), key);
    EXPECT_NE(HandlerLightPlacerTracker::modelKey("armor\\bar.nif"), key);
}

}
