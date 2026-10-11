#include "util/FileUtil.hpp"

#include "TestUtil.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include <windows.h>

namespace {

std::vector<std::byte> toBytes(const std::string& text)
{
    std::vector<std::byte> bytes;
    for (const char ch : text)
        bytes.push_back(static_cast<std::byte>(ch));
    return bytes;
}

const FileUtil::DirectoryEntry* findEntry(const std::vector<FileUtil::DirectoryEntry>& entries,
                                          const std::wstring& name)
{
    const auto it = std::ranges::find_if(entries, [&name](const auto& entry) { return entry.name == name; });
    return it == entries.end() ? nullptr : &*it;
}

TEST(FileUtilTest,
     FileBytesReadsWholeFileOrNothing)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"data.bin", std::string("ab\0cd", 5));
    EXPECT_EQ(FileUtil::fileBytes(dir / L"data.bin"), toBytes(std::string("ab\0cd", 5)));

    TestUtil::writeFile(dir / L"empty.bin", "");
    EXPECT_TRUE(FileUtil::fileBytes(dir / L"empty.bin").empty());

    EXPECT_TRUE(FileUtil::fileBytes(dir / L"missing.bin").empty());
    EXPECT_TRUE(FileUtil::fileBytes(dir.path()).empty()); // a directory cannot be read as a file
}

TEST(FileUtilTest,
     GetJsonParsesFilesAndRejectsBadOnes)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"good.json", R"({"a": [1, 2], "b": "text"})");
    TestUtil::writeFile(dir / L"bad.json", "{ not json");
    TestUtil::writeFile(dir / L"empty.json", "");

    nlohmann::json json;
    ASSERT_TRUE(FileUtil::getJSON(dir / L"good.json", json));
    EXPECT_EQ(json["a"][1], 2);
    EXPECT_EQ(json["b"], "text");

    EXPECT_FALSE(FileUtil::getJSON(dir / L"bad.json", json));
    EXPECT_FALSE(FileUtil::getJSON(dir / L"empty.json", json));
    EXPECT_FALSE(FileUtil::getJSON(dir / L"missing.json", json));
}

TEST(FileUtilTest,
     GetJsonFromBytesParsesUtf8AndRejectsBadInput)
{
    nlohmann::json json;
    ASSERT_TRUE(FileUtil::getJSONFromBytes(toBytes(R"({"key": "café", "n": 3})"), json));
    EXPECT_EQ(json["key"], "caf\xC3\xA9");
    EXPECT_EQ(json["n"], 3);

    ASSERT_TRUE(FileUtil::getJSONFromBytes(toBytes("{\"raw\": \"caf\xC3\xA9\"}"), json));
    EXPECT_EQ(json["raw"], "caf\xC3\xA9");

    EXPECT_FALSE(FileUtil::getJSONFromBytes(toBytes("[1, 2"), json));
    EXPECT_FALSE(FileUtil::getJSONFromBytes({ }, json));
}

TEST(FileUtilTest,
     SaveJsonWritesReadableOrCompactOutput)
{
    const TestUtil::TempDir dir;
    const nlohmann::json json = { { "b", 1 }, { "a", nlohmann::json::array({ "x", "y" }) } };

    ASSERT_NO_THROW(FileUtil::saveJSON(dir / L"readable.json", json, true));
    const auto readable = TestUtil::readFile(dir / L"readable.json");
    EXPECT_NE(readable.find('\n'), std::string::npos);
    EXPECT_EQ(nlohmann::json::parse(readable), json);

    ASSERT_NO_THROW(FileUtil::saveJSON(dir / L"compact.json", json, false));
    const auto compact = TestUtil::readFile(dir / L"compact.json");
    EXPECT_EQ(compact.find('\n'), std::string::npos);
    EXPECT_EQ(compact, json.dump());

    // Invalid UTF-8 is replaced instead of aborting the write.
    const nlohmann::json invalid = { { "s", "bad\xFF" } };
    ASSERT_NO_THROW(FileUtil::saveJSON(dir / L"invalid.json", invalid, false));
    EXPECT_EQ(nlohmann::json::parse(TestUtil::readFile(dir / L"invalid.json"))["s"], "bad\xEF\xBF\xBD");
}

TEST(FileUtilTest,
     SaveJsonThrowsWhenTheFileCannotBeOpened)
{
    const TestUtil::TempDir dir;
    const nlohmann::json json = { { "a", 1 } };
    // The folder does not exist, so the file cannot be opened. The callers (configuration saving, output JSONs) rely
    // on the throw to report the failure.
    EXPECT_THROW(FileUtil::saveJSON(dir / L"missing\\folder\\out.json", json, true), std::filesystem::filesystem_error);
    EXPECT_FALSE(std::filesystem::exists(dir / L"missing"));
}

TEST(FileUtilTest,
     ListDirectoryDescribesEntriesWithoutDots)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"file.txt", "12345");
    TestUtil::writeFile(dir / L"hidden.txt", "x");
    ASSERT_TRUE(SetFileAttributesW((dir / L"hidden.txt").c_str(), FILE_ATTRIBUTE_HIDDEN));
    std::filesystem::create_directories(dir / L"folder");
    TestUtil::writeFile(dir / L"folder\\nested.txt", "x");

    const auto entries = FileUtil::listDirectory(dir.path());
    ASSERT_EQ(entries.size(), 3U);
    EXPECT_EQ(findEntry(entries, L"."), nullptr);
    EXPECT_EQ(findEntry(entries, L".."), nullptr);
    EXPECT_EQ(findEntry(entries, L"nested.txt"), nullptr); // not recursive

    const auto* file = findEntry(entries, L"file.txt");
    ASSERT_NE(file, nullptr);
    EXPECT_TRUE(file->isRegularFile);
    EXPECT_FALSE(file->isDirectory);
    EXPECT_FALSE(file->isHidden);
    EXPECT_FALSE(file->isLink);
    EXPECT_EQ(file->size, 5U);
    EXPECT_EQ(file->mtime, std::filesystem::last_write_time(dir / L"file.txt").time_since_epoch().count());

    const auto* hidden = findEntry(entries, L"hidden.txt");
    ASSERT_NE(hidden, nullptr);
    EXPECT_TRUE(hidden->isHidden);
    EXPECT_TRUE(hidden->isRegularFile);

    const auto* folder = findEntry(entries, L"folder");
    ASSERT_NE(folder, nullptr);
    EXPECT_TRUE(folder->isDirectory);
    EXPECT_FALSE(folder->isRegularFile);
    EXPECT_EQ(folder->size, 0U);
    EXPECT_FALSE(folder->isLink);
}

TEST(FileUtilTest,
     ListDirectoryOfEmptyFolderIsEmpty)
{
    const TestUtil::TempDir dir;
    EXPECT_TRUE(FileUtil::listDirectory(dir.path()).empty());
}

TEST(FileUtilTest,
     ListDirectoryThrowsForMissingOrNonDirectoryPaths)
{
    const TestUtil::TempDir dir;
    EXPECT_THROW(FileUtil::listDirectory(dir / L"missing"), std::filesystem::filesystem_error);
    EXPECT_THROW(FileUtil::listDirectory(dir / L"missing", true), std::filesystem::filesystem_error);

    TestUtil::writeFile(dir / L"file.txt", "x");
    EXPECT_THROW(FileUtil::listDirectory(dir / L"file.txt"), std::filesystem::filesystem_error);
}

TEST(FileUtilTest,
     ListDirectoryDescribesJunctionsAsLinksToDirectories)
{
    const TestUtil::TempDir dir;
    std::filesystem::create_directories(dir / L"target");
    TestUtil::writeFile(dir / L"target\\inner.txt", "x");
    if (!TestUtil::createJunction(dir / L"link", dir / L"target"))
        GTEST_SKIP() << "junctions cannot be created here";

    const auto entries = FileUtil::listDirectory(dir.path());
    const auto* link = findEntry(entries, L"link");
    ASSERT_NE(link, nullptr);
    EXPECT_TRUE(link->isLink);
    EXPECT_TRUE(link->isDirectory);
    EXPECT_FALSE(link->isRegularFile);
    EXPECT_EQ(link->size, 0U);

    const auto* target = findEntry(entries, L"target");
    ASSERT_NE(target, nullptr);
    EXPECT_FALSE(target->isLink);
}

TEST(FileUtilTest,
     WalkDirectoryVisitsEverythingWithRelativePaths)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"a\\b\\c.txt", "x");
    TestUtil::writeFile(dir / L"a\\d.txt", "x");
    TestUtil::writeFile(dir / L"e.txt", "x");

    std::set<std::wstring> visited;
    std::set<std::wstring> directories;
    FileUtil::walkDirectory(dir.path(), [&](const std::wstring& relPath, const FileUtil::DirectoryEntry& entry) {
        visited.insert(relPath);
        if (entry.isDirectory)
            directories.insert(relPath);
        return true;
    });

    EXPECT_EQ(visited, (std::set<std::wstring> { L"a", L"a\\b", L"a\\b\\c.txt", L"a\\d.txt", L"e.txt" }));
    EXPECT_EQ(directories, (std::set<std::wstring> { L"a", L"a\\b" }));
}

TEST(FileUtilTest,
     WalkDirectoryOnlyDescendsWhenTheVisitorAllowsIt)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"a\\b\\c.txt", "x");
    TestUtil::writeFile(dir / L"keep\\k.txt", "x");
    TestUtil::writeFile(dir / L"e.txt", "x");

    std::set<std::wstring> visited;
    FileUtil::walkDirectory(dir.path(), [&](const std::wstring& relPath, const FileUtil::DirectoryEntry&) {
        visited.insert(relPath);
        return relPath != L"a";
    });

    EXPECT_EQ(visited, (std::set<std::wstring> { L"a", L"keep", L"keep\\k.txt", L"e.txt" }));
}

TEST(FileUtilTest,
     WalkDirectoryDoesNotFollowJunctions)
{
    const TestUtil::TempDir dir;
    TestUtil::writeFile(dir / L"target\\inner.txt", "x");
    if (!TestUtil::createJunction(dir / L"link", dir / L"target"))
        GTEST_SKIP() << "junctions cannot be created here";

    std::set<std::wstring> visited;
    FileUtil::walkDirectory(dir.path(), [&](const std::wstring& relPath, const FileUtil::DirectoryEntry&) {
        visited.insert(relPath);
        return true;
    });

    EXPECT_EQ(visited, (std::set<std::wstring> { L"link", L"target", L"target\\inner.txt" }));
}

TEST(FileUtilTest,
     WalkDirectoryThrowsForMissingRoot)
{
    const TestUtil::TempDir dir;
    EXPECT_THROW(FileUtil::walkDirectory(dir / L"missing",
                                         [](const std::wstring&, const FileUtil::DirectoryEntry&) { return true; }),
                 std::filesystem::filesystem_error);
}

}
