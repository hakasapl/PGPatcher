#include "util/EnumStringHelper.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

enum class Color : uint8_t { Red, Green, Blue, Unknown };

constexpr std::array<EnumStringHelper::EnumStringEntry<Color>, 3> colorTable {
    {
        { .value = Color::Red, .name = "red" },
        { .value = Color::Green, .name = "green" },
        { .value = Color::Blue, .name = "blue" },
    },
};

TEST(EnumStringHelperTest,
     EnumFromStringFindsEntriesCaseSensitively)
{
    EXPECT_EQ(EnumStringHelper::enumFromString("red", colorTable, Color::Unknown), Color::Red);
    EXPECT_EQ(EnumStringHelper::enumFromString("blue", colorTable, Color::Unknown), Color::Blue);
    EXPECT_EQ(EnumStringHelper::enumFromString("Red", colorTable, Color::Unknown), Color::Unknown);
    EXPECT_EQ(EnumStringHelper::enumFromString("", colorTable, Color::Unknown), Color::Unknown);
    EXPECT_EQ(EnumStringHelper::enumFromString("purple", colorTable, Color::Green), Color::Green);
}

TEST(EnumStringHelperTest,
     StringFromEnumFindsEntries)
{
    EXPECT_EQ(EnumStringHelper::stringFromEnum(Color::Red, colorTable, "?"), "red");
    EXPECT_EQ(EnumStringHelper::stringFromEnum(Color::Green, colorTable, "?"), "green");
    EXPECT_EQ(EnumStringHelper::stringFromEnum(Color::Unknown, colorTable, "?"), "?");
}

TEST(EnumStringHelperTest,
     AllEnumStringsListsNamesInTableOrder)
{
    const std::vector<std::string> expected { "red", "green", "blue" };
    EXPECT_EQ(EnumStringHelper::allEnumStrings(colorTable), expected);
}

TEST(EnumStringHelperTest,
     ConversionsAreConstexpr)
{
    static_assert(EnumStringHelper::enumFromString("green", colorTable, Color::Unknown) == Color::Green);
    static_assert(EnumStringHelper::stringFromEnum(Color::Blue, colorTable, "?") == "blue");
    SUCCEED();
}

}
