#include "PGParams.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace {

TEST(PGParamsTest,
     DefaultsRoundTripThroughJSON)
{
    const auto defaults = PGParams::defaults();
    nlohmann::json json = nlohmann::json::object();
    defaults.writeJSON(json);

    // The lists of a default constructed struct are empty, so the appended lists equal the written ones.
    PGParams read;
    read.readJSON(json);
    EXPECT_TRUE(read == defaults);
}

TEST(PGParamsTest,
     SerializedKeysAreTheSettingsFileKeys)
{
    nlohmann::json json = nlohmann::json::object();
    PGParams::defaults().writeJSON(json);

    for (const auto* section : { "game",
                                 "modmanager",
                                 "output",
                                 "processing",
                                 "prepatcher",
                                 "shaderpatcher",
                                 "shadertransforms",
                                 "postpatcher" })
        EXPECT_TRUE(json.contains(section)) << section;

    EXPECT_TRUE(json["game"].contains("dir"));
    EXPECT_TRUE(json["game"].contains("type"));
    EXPECT_TRUE(json["modmanager"].contains("mo2instancedir"));
    EXPECT_TRUE(json["modmanager"].contains("mo2useloosefileorder"));
    EXPECT_TRUE(json["output"].contains("pluginlang"));
    EXPECT_TRUE(json["processing"].contains("allowedmodelrecordtypes"));
    EXPECT_TRUE(json["processing"].contains("vanillabsalist"));
    EXPECT_TRUE(json["processing"].contains("texturemaps"));
    EXPECT_TRUE(json["processing"].contains("blocklist"));
    EXPECT_TRUE(json["processing"].contains("allowlist"));
    EXPECT_TRUE(json["prepatcher"].contains("fixmeshlighting"));
    EXPECT_TRUE(json["shaderpatcher"].contains("complexmaterial"));
    EXPECT_TRUE(json["shadertransforms"].contains("parallaxtocm"));
    EXPECT_TRUE(json["postpatcher"].contains("disableprepatchedmaterials"));
}

TEST(PGParamsTest,
     ReadKeepsTheValuesOfMissingKeysAndAppendsToLists)
{
    auto params = PGParams::defaults();
    const size_t blockListSize = params.processing.blockList.size();

    nlohmann::json json = nlohmann::json::object();
    json["processing"]["blocklist"] = nlohmann::json::array({ "*\\extra\\*" });
    json["shaderpatcher"]["truepbr"] = true;
    params.readJSON(json);

    EXPECT_TRUE(params.shaderPatcher.isTruePBREnabled);
    EXPECT_TRUE(params.shaderPatcher.isParallaxEnabled);
    ASSERT_EQ(params.processing.blockList.size(), blockListSize + 1);
    EXPECT_EQ(params.processing.blockList.back(), L"*\\extra\\*");
}

TEST(PGParamsTest,
     WriteKeepsUnknownKeys)
{
    nlohmann::json json = nlohmann::json::object();
    json["processing"]["futurekey"] = 1;
    PGParams::defaults().writeJSON(json);
    EXPECT_EQ(json["processing"]["futurekey"], 1);
}

TEST(PGParamsTest,
     ReplaceForwardSlashesTouchesEveryString)
{
    nlohmann::json json
        = { { "a", "x/y/z" }, { "b", { { "c", "p/q" } } }, { "d", nlohmann::json::array({ "m/n", 1 }) } };
    PGParams::replaceForwardSlashes(json);
    EXPECT_EQ(json["a"].get<std::string>(), "x\\y\\z");
    EXPECT_EQ(json["b"]["c"].get<std::string>(), "p\\q");
    EXPECT_EQ(json["d"][0].get<std::string>(), "m\\n");
    EXPECT_EQ(json["d"][1], 1);
}

}
