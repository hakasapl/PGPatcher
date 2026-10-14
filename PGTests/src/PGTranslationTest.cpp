#include "PGTranslation.hpp"

#include "TestUtil.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

void writeTranslation(const std::filesystem::path& path,
                      const std::string& content)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << content;
}

/**
 * @brief A translations folder with English, a partial German translation and a file without a display name.
 */
class PGTranslationTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    void SetUp() override
    {
        writeTranslation(m_dir / "en.json",
                         R"({"_language": "English", "a": {"b": "Bee", "c": "Sea"}, "common": {"ok": "OK"}})");
        writeTranslation(m_dir / "de.json", R"({"_language": "Deutsch", "a": {"b": "Biene", "c": ""}})");
        writeTranslation(m_dir / "zz.json", R"({"a": {"b": "Zed"}})");
    }

    void TearDown() override { PGTranslation::init({ }, { }); }
};

}

TEST_F(PGTranslationTest,
       EnglishIsTheFallbackLayer)
{
    PGTranslation::init(m_dir.path(), "de");
    EXPECT_EQ(PGTranslation::currentLanguage(), "de");
    EXPECT_EQ(PGTranslation::tr("a.b"), "Biene");
    // An empty value keeps the English string, a missing key too.
    EXPECT_EQ(PGTranslation::tr("a.c"), "Sea");
    EXPECT_EQ(PGTranslation::tr("common.ok"), "OK");
}

TEST_F(PGTranslationTest,
       UnknownKeysShowTheKey)
{
    PGTranslation::init(m_dir.path(), "de");
    EXPECT_EQ(PGTranslation::tr("missing.key"), "missing.key");
    // The reserved metadata key is not a string of the table.
    EXPECT_EQ(PGTranslation::tr("_language"), "_language");
}

TEST_F(PGTranslationTest,
       EmptyAndMissingLanguagesGiveEnglish)
{
    PGTranslation::init(m_dir.path(), "");
    EXPECT_EQ(PGTranslation::currentLanguage(), "en");
    EXPECT_EQ(PGTranslation::tr("a.b"), "Bee");

    PGTranslation::init(m_dir.path(), "xx");
    EXPECT_EQ(PGTranslation::currentLanguage(), "xx");
    EXPECT_EQ(PGTranslation::tr("a.b"), "Bee");
}

TEST_F(PGTranslationTest,
       LanguagesAreListedByDisplayName)
{
    PGTranslation::init(m_dir.path(), "en");
    const auto languages = PGTranslation::availableLanguages();
    ASSERT_EQ(languages.size(), 3U);
    EXPECT_EQ(languages[0].code, "de");
    EXPECT_EQ(languages[0].displayName, "Deutsch");
    EXPECT_EQ(languages[1].code, "en");
    EXPECT_EQ(languages[1].displayName, "English");
    // A file without "_language" sorts by its code and has no display name.
    EXPECT_EQ(languages[2].code, "zz");
    EXPECT_TRUE(languages[2].displayName.empty());
}

TEST_F(PGTranslationTest,
       NoFolderMeansNoLanguagesAndKeysOnly)
{
    PGTranslation::init(m_dir / "missing", "de");
    EXPECT_TRUE(PGTranslation::availableLanguages().empty());
    EXPECT_EQ(PGTranslation::tr("a.b"), "a.b");
}

TEST_F(PGTranslationTest,
       AllStringsListsTheLoadedTable)
{
    PGTranslation::init(m_dir.path(), "de");
    const auto strings = PGTranslation::allStrings();
    EXPECT_NE(std::ranges::find(strings, "Biene"), strings.end());
    EXPECT_NE(std::ranges::find(strings, "Sea"), strings.end());
    EXPECT_EQ(std::ranges::find(strings, "Bee"), strings.end());
}

TEST_F(PGTranslationTest,
       TrFormatReplacesTheFirstPlaceholder)
{
    writeTranslation(m_dir / "en.json", R"({"_language": "English", "f": {"one": "Reason: %s.", "none": "Plain"}})");
    PGTranslation::init(m_dir.path(), "en");
    EXPECT_EQ(PGTranslation::trFormat("f.one", "x"), "Reason: x.");
    EXPECT_EQ(PGTranslation::trFormat("f.none", "x"), "Plain");
    EXPECT_EQ(PGTranslation::trFormat("missing.key", "x"), "missing.key");
}
