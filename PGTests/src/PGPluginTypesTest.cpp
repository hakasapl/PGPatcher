#include "PGPlugin.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

using UseOrder = PGPlugin::UseOrder;

TEST(PGPluginTypesTest,
     UseOrderPutsPlainUsesFirstThenWeightedThenPluginFormAndSubModel)
{
    const UseOrder plain { .modName = L"B.esp", .formID = 2 };
    const UseOrder plainEarlierPlugin { .modName = L"A.esp", .formID = 9 };
    const UseOrder plainLowerForm { .modName = L"B.esp", .formID = 1 };
    const UseOrder plainSubModel { .modName = L"B.esp", .formID = 2, .subModel = "MALE" };
    const UseOrder weighted { .isWeighted = true, .modName = L"Z.esp", .formID = 9 };
    const UseOrder withAlternateTextures { .hasAlternateTextures = true, .modName = L"A.esp", .formID = 0 };

    // Weighted uses come before plain ones, whatever their plugin.
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(weighted, plain));
    EXPECT_FALSE(PGPlugin::isUseOrderedBefore(plain, weighted));
    // Uses with alternate textures come last, even before weighted ones are considered.
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(plain, withAlternateTextures));
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(weighted, withAlternateTextures));
    EXPECT_FALSE(PGPlugin::isUseOrderedBefore(withAlternateTextures, plain));
    // Then the plugin name, the form ID and the sub model, each ascending.
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(plainEarlierPlugin, plain));
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(plainLowerForm, plain));
    EXPECT_TRUE(PGPlugin::isUseOrderedBefore(plain, plainSubModel));
    EXPECT_FALSE(PGPlugin::isUseOrderedBefore(plainSubModel, plain));
    // A use is not ordered before itself.
    EXPECT_FALSE(PGPlugin::isUseOrderedBefore(plain, plain));
}

TEST(PGPluginTypesTest,
     UseOrderIsAStrictWeakOrderingForSorting)
{
    std::vector<UseOrder> uses {
        { .hasAlternateTextures = true, .modName = L"A.esp", .formID = 1 },
        { .modName = L"B.esp", .formID = 2, .subModel = "FEMALE" },
        { .isWeighted = true, .modName = L"C.esp", .formID = 3 },
        { .modName = L"B.esp", .formID = 2 },
        { .modName = L"A.esp", .formID = 5 },
    };
    std::ranges::sort(uses, PGPlugin::isUseOrderedBefore);
    EXPECT_TRUE(uses[0].isWeighted);
    EXPECT_EQ(uses[1].modName, L"A.esp");
    EXPECT_EQ(uses[2].formID, 2U);
    EXPECT_TRUE(uses[2].subModel.empty());
    EXPECT_EQ(uses[3].subModel, "FEMALE");
    EXPECT_TRUE(uses[4].hasAlternateTextures);
}

}
