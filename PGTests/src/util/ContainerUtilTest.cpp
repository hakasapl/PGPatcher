#include "util/ContainerUtil.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

TEST(ContainerUtilTest,
     IsInVector)
{
    const std::vector<int> values { 1, 2, 3 };
    EXPECT_TRUE(ContainerUtil::isInVector(values, 1));
    EXPECT_TRUE(ContainerUtil::isInVector(values, 3));
    EXPECT_FALSE(ContainerUtil::isInVector(values, 4));
    EXPECT_FALSE(ContainerUtil::isInVector(std::vector<int> { }, 1));

    const std::vector<std::wstring> strings { L"a", L"b" };
    EXPECT_TRUE(ContainerUtil::isInVector(strings, std::wstring(L"b")));
    EXPECT_FALSE(ContainerUtil::isInVector(strings, std::wstring(L"B")));
}

TEST(ContainerUtilTest,
     ConcatenateVectorsWithoutDuplicatesKeepsOrderAndSkipsDuplicates)
{
    std::vector<int> target { 1, 2, 3 };
    ContainerUtil::concatenateVectorsWithoutDuplicates(target, { 3, 4, 4, 1, 5 });
    EXPECT_EQ(target, (std::vector<int> { 1, 2, 3, 4, 5 }));

    std::vector<std::string> empty;
    ContainerUtil::concatenateVectorsWithoutDuplicates(empty, { "x", "x", "y" });
    EXPECT_EQ(empty, (std::vector<std::string> { "x", "y" }));

    std::vector<int> unchanged { 7 };
    ContainerUtil::concatenateVectorsWithoutDuplicates(unchanged, { });
    EXPECT_EQ(unchanged, (std::vector<int> { 7 }));
}

TEST(ContainerUtilTest,
     AddUniqueElement)
{
    std::vector<int> values;
    ContainerUtil::addUniqueElement(values, 1);
    ContainerUtil::addUniqueElement(values, 2);
    ContainerUtil::addUniqueElement(values, 1);
    EXPECT_EQ(values, (std::vector<int> { 1, 2 }));

    std::vector<std::string> strings { "a" };
    ContainerUtil::addUniqueElement(strings, std::string("a"));
    ContainerUtil::addUniqueElement(strings, std::string("A"));
    EXPECT_EQ(strings, (std::vector<std::string> { "a", "A" }));
}

}
