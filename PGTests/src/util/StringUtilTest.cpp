#include "util/StringUtil.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace {

TEST(StringUtilTest,
     Utf8ToUtf16ConvertsAsciiMultibyteAndSurrogates)
{
    EXPECT_EQ(StringUtil::utf8toUTF16(""), L"");
    EXPECT_EQ(StringUtil::utf8toUTF16("hello"), L"hello");
    EXPECT_EQ(StringUtil::utf8toUTF16("h\xC3\xA9"
                                      "llo"),
              L"héllo");
    EXPECT_EQ(StringUtil::utf8toUTF16("\xE6\x97\xA5\xE6\x9C\xAC"), L"日本");
    // A supplementary plane code point becomes a surrogate pair.
    EXPECT_EQ(StringUtil::utf8toUTF16("\xF0\x9F\x98\x80"), L"\xD83D\xDE00");
}

TEST(StringUtilTest,
     Utf16ToUtf8ConvertsAsciiMultibyteAndSurrogates)
{
    EXPECT_EQ(StringUtil::utf16toUTF8(L""), "");
    EXPECT_EQ(StringUtil::utf16toUTF8(L"hello"), "hello");
    EXPECT_EQ(StringUtil::utf16toUTF8(L"héllo"),
              "h\xC3\xA9"
              "llo");
    EXPECT_EQ(StringUtil::utf16toUTF8(L"日本"), "\xE6\x97\xA5\xE6\x9C\xAC");
    EXPECT_EQ(StringUtil::utf16toUTF8(L"\xD83D\xDE00"), "\xF0\x9F\x98\x80");
}

TEST(StringUtilTest,
     Utf8Utf16RoundTrip)
{
    const std::string original = "meshes\\\xC3\xBC"
                                 "ber\\\xE6\x97\xA5.nif";
    EXPECT_EQ(StringUtil::utf16toUTF8(StringUtil::utf8toUTF16(original)), original);

    const std::wstring originalW = L"textures\\über\\日.dds";
    EXPECT_EQ(StringUtil::utf8toUTF16(StringUtil::utf16toUTF8(originalW)), originalW);
}

TEST(StringUtilTest,
     Windows1252Conversions)
{
    EXPECT_EQ(StringUtil::windows1252toUTF16("caf\xE9"), L"café");
    EXPECT_EQ(StringUtil::windows1252toUTF16("\x80"), L"€"); // euro sign only exists in the 0x80 slot of cp1252
    EXPECT_EQ(StringUtil::utf16toWindows1252(L"café"), "caf\xE9");
    EXPECT_EQ(StringUtil::utf16toWindows1252(L"€"), "\x80");
    // A character with no best fit is dropped, as boost.locale's skip did; the ASCII test has the same case.
    EXPECT_EQ(StringUtil::utf16toWindows1252(L"a\u65E5b"), "ab");
    EXPECT_EQ(StringUtil::windows1252toUTF16(""), L"");
    EXPECT_EQ(StringUtil::utf16toWindows1252(L""), "");
}

TEST(StringUtilTest,
     AsciiConversionsDropWhatCannotBeRepresented)
{
    EXPECT_EQ(StringUtil::asciitoUTF16("abc 123"), L"abc 123");
    EXPECT_EQ(StringUtil::utf16toASCII(L"abc 123"), "abc 123");
    // Characters outside ASCII become their closest ASCII character (Windows best fit mapping).
    EXPECT_EQ(StringUtil::utf16toASCII(L"aéb"), "aeb");
    // A character with no best fit is dropped, not replaced.
    EXPECT_EQ(StringUtil::utf16toASCII(L"a\u65E5b"), "ab");
    EXPECT_EQ(StringUtil::utf16toASCII(L"\u65E5"), "");
    EXPECT_EQ(StringUtil::asciitoUTF16(""), L"");
}

TEST(StringUtilTest,
     VectorConversionsKeepOrderAndSize)
{
    const std::vector<std::string> utf8 { "a", "b\xC3\xA9", "" };
    const std::vector<std::wstring> utf16 { L"a", L"bé", L"" };
    EXPECT_EQ(StringUtil::utf8VectorToUTF16(utf8), utf16);
    EXPECT_EQ(StringUtil::utf16VectorToUTF8(utf16), utf8);

    const std::vector<std::string> cp1252 { "caf\xE9", "x" };
    const std::vector<std::wstring> cp1252W { L"café", L"x" };
    EXPECT_EQ(StringUtil::windows1252VectorToUTF16(cp1252), cp1252W);
    EXPECT_EQ(StringUtil::utf16VectorToWindows1252(cp1252W), cp1252);

    const std::vector<std::string> ascii { "one", "two" };
    const std::vector<std::wstring> asciiW { L"one", L"two" };
    EXPECT_EQ(StringUtil::asciiVectorToUTF16(ascii), asciiW);
    EXPECT_EQ(StringUtil::utf16VectorToASCII(asciiW), ascii);

    EXPECT_TRUE(StringUtil::utf8VectorToUTF16({ }).empty());
    EXPECT_TRUE(StringUtil::utf16VectorToUTF8({ }).empty());
}

TEST(StringUtilTest,
     ContainsOnlyAsciiNarrow)
{
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::string("")));
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::string("textures\\a_b.dds")));
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::string("\x7F")));
    // Bytes above 0x7F are negative as a char on MSVC and must still count as non-ASCII.
    EXPECT_FALSE(StringUtil::containsOnlyAscii(std::string("caf\xC3\xA9")));
    EXPECT_FALSE(StringUtil::containsOnlyAscii(std::string("\x80")));
}

TEST(StringUtilTest,
     ContainsOnlyAsciiWide)
{
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::wstring(L"")));
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::wstring(L"textures\\a_b.dds")));
    EXPECT_TRUE(StringUtil::containsOnlyAscii(std::wstring(L"\x7F")));
    EXPECT_FALSE(StringUtil::containsOnlyAscii(std::wstring(L"café")));
    EXPECT_FALSE(StringUtil::containsOnlyAscii(std::wstring(L"\u0080")));
}

TEST(StringUtilTest,
     ToLowerAsciiLowersAsciiLetters)
{
    EXPECT_EQ(StringUtil::toLowerASCII(L"ABC def 123 _-."), L"abc def 123 _-.");
    EXPECT_EQ(StringUtil::toLowerASCII(L""), L"");
}

TEST(StringUtilTest,
     ToLowerAsciiFastOnlyTouchesAsciiLetters)
{
    EXPECT_EQ(StringUtil::toLowerASCIIFast(std::string("AbC_Z09.DDS")), "abc_z09.dds");
    EXPECT_EQ(StringUtil::toLowerASCIIFast(std::wstring(L"AbC_Z09.DDS")), L"abc_z09.dds");
    EXPECT_EQ(StringUtil::toLowerASCIIFast(std::wstring(L"ÉÜ")), L"ÉÜ");
    EXPECT_EQ(StringUtil::toLowerASCIIFast(std::string("")), "");

    std::string narrow = "Meshes\\ARMOR\\Iron.NIF";
    StringUtil::toLowerASCIIFastInPlace(narrow);
    EXPECT_EQ(narrow, "meshes\\armor\\iron.nif");

    std::wstring wide = L"Meshes\\ARMOR\\Îron.NIF";
    StringUtil::toLowerASCIIFastInPlace(wide);
    EXPECT_EQ(wide, L"meshes\\armor\\Îron.nif");
}

TEST(StringUtilTest,
     CheckIfStringInJsonArray)
{
    const nlohmann::json array = nlohmann::json::array({ "alpha", "beta", 1, nullptr });
    EXPECT_TRUE(StringUtil::checkIfStringInJSONArray(array, "alpha"));
    EXPECT_TRUE(StringUtil::checkIfStringInJSONArray(array, "beta"));
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(array, "gamma"));
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(array, "1")); // non-string elements never match
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(array, "Alpha")); // case-sensitive

    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(nlohmann::json::array(), "alpha"));
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(nlohmann::json::object({ { "alpha", true } }), "alpha"));
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(nlohmann::json("alpha"), "alpha"));
    EXPECT_FALSE(StringUtil::checkIfStringInJSONArray(nlohmann::json(), "alpha"));
}

TEST(StringUtilTest,
     AsciiFastIEqualsIgnoresCaseOfAsciiLetters)
{
    EXPECT_TRUE(StringUtil::asciiFastIEquals(std::string("ABC"), std::string("abc")));
    EXPECT_TRUE(StringUtil::asciiFastIEquals(std::wstring(L"Textures\\A.DDS"), std::wstring(L"textures\\a.dds")));
    EXPECT_TRUE(StringUtil::asciiFastIEquals(std::string(""), std::string("")));
    EXPECT_FALSE(StringUtil::asciiFastIEquals(std::string("abc"), std::string("abd")));
    EXPECT_FALSE(StringUtil::asciiFastIEquals(std::string("abc"), std::string("abcd")));
    // Only ASCII letters fold, so differently cased non-ASCII letters stay different.
    EXPECT_FALSE(StringUtil::asciiFastIEquals(std::wstring(L"É"), std::wstring(L"é")));
}

}
