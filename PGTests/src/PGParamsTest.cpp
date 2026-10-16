#include "PGParams.hpp"

#include "pgutil/PGEnums.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

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

    for (const auto* section : {
             "game",
             "modmanager",
             "output",
             "processing",
             "prepatcher",
             "shaderpatcher",
             "shadertransforms",
             "postpatcher",
         })
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
    // The rest of the keys, so a rename of any of them fails here and not in a user's settings file.
    EXPECT_TRUE(json["output"].contains("dir"));
    EXPECT_TRUE(json["output"].contains("zip"));
    EXPECT_TRUE(json["modmanager"].contains("type"));
    EXPECT_TRUE(json["processing"].contains("multithread"));
    EXPECT_TRUE(json["processing"].contains("devmode"));
    EXPECT_TRUE(json["processing"].contains("enabledebuglogging"));
    EXPECT_TRUE(json["processing"].contains("enabletracelogging"));
    EXPECT_TRUE(json["processing"].contains("pglitemode"));
    EXPECT_TRUE(json["shaderpatcher"].contains("parallax"));
    EXPECT_TRUE(json["shaderpatcher"].contains("truepbr"));
    EXPECT_TRUE(json["postpatcher"].contains("fixsss"));
    EXPECT_TRUE(json["postpatcher"].contains("hairflowmap"));
}

TEST(PGParamsTest,
     PGLiteModeIsOffByDefaultAndRoundTrips)
{
    EXPECT_FALSE(PGParams::defaults().processing.isPGLiteMode);

    PGParams params;
    params.processing.isPGLiteMode = true;
    nlohmann::json json = nlohmann::json::object();
    params.writeJSON(json);
    EXPECT_EQ(json["processing"]["pglitemode"], true);

    PGParams read;
    read.readJSON(json);
    EXPECT_TRUE(read.processing.isPGLiteMode);
    EXPECT_TRUE(read == params);
    read.processing.isPGLiteMode = false;
    EXPECT_FALSE(read == params);
}

TEST(PGParamsTest,
     RecalculateBoundsIsOffByDefaultAndRoundTrips)
{
    EXPECT_FALSE(PGParams::defaults().globalPatcher.isRecalculateBoundsEnabled);

    PGParams params;
    params.globalPatcher.isRecalculateBoundsEnabled = true;
    nlohmann::json json = nlohmann::json::object();
    params.writeJSON(json);
    EXPECT_EQ(json["globalpatcher"]["recalculatebounds"], true);

    PGParams read;
    read.readJSON(json);
    EXPECT_TRUE(read.globalPatcher.isRecalculateBoundsEnabled);
    EXPECT_TRUE(read == params);
    read.globalPatcher.isRecalculateBoundsEnabled = false;
    EXPECT_FALSE(read == params);

    // A settings file written before the option existed leaves it off.
    PGParams old;
    old.readJSON(nlohmann::json::object());
    EXPECT_FALSE(old.globalPatcher.isRecalculateBoundsEnabled);
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

TEST(PGParamsTest,
     ValidateAcceptsTheDefaults)
{
    EXPECT_TRUE(PGParams::defaults().validate().empty());
    EXPECT_TRUE(PGParams { }.validate().empty());
}

TEST(PGParamsTest,
     ValidateRequiresBothPatchersOfTheTransform)
{
    PGParams params = PGParams::defaults();
    params.shaderTransforms.isParallaxToCMEnabled = true;
    EXPECT_TRUE(params.validate().empty());

    params.shaderPatcher.isComplexMaterialEnabled = false;
    EXPECT_EQ(
        params.validate(),
        (std::vector<PGParams::ValidationError> { { .key = "launcher.validation.parallaxToCMRequiresPatchers" } }));

    params.shaderPatcher.isComplexMaterialEnabled = true;
    params.shaderPatcher.isParallaxEnabled = false;
    EXPECT_EQ(params.validate().size(), 1U);

    params.shaderTransforms.isParallaxToCMEnabled = false;
    EXPECT_TRUE(params.validate().empty());
}

TEST(PGParamsTest,
     ValidateReportsEmptyAndDuplicateEntriesInTheLauncherOrder)
{
    PGParams params;
    params.processing.allowList = { L"a", L"", L"a" };
    params.processing.blockList = { L"b", L"b" };
    params.processing.textureMaps = {
        { L"t", PGEnums::TextureType::Diffuse },
        { L"", PGEnums::TextureType::Diffuse },
        { L"t", PGEnums::TextureType::Normal },
    };
    params.processing.vanillaBSAList = { L"v.bsa", L"v.bsa", L"" };
    const std::vector<PGParams::ValidationError> expected {
        { .key = "launcher.validation.meshAllowListEmptyEntry" },
        { .key = "launcher.validation.meshAllowListDuplicate", .hasItem = true, .item = L"a" },
        { .key = "launcher.validation.meshBlockListDuplicate", .hasItem = true, .item = L"b" },
        { .key = "launcher.validation.textureRulesEmptyKey" },
        { .key = "launcher.validation.textureRulesDuplicate", .hasItem = true, .item = L"t" },
        { .key = "launcher.validation.vanillaBSAListDuplicate", .hasItem = true, .item = L"v.bsa" },
        { .key = "launcher.validation.vanillaBSAListEmptyEntry" },
    };
    EXPECT_EQ(params.validate(), expected);
}

TEST(PGParamsTest,
     TextureRulePathsAreReadInTheMappedForm)
{
    const auto type = PGEnums::strFromTexType(PGEnums::TextureType::Diffuse);
    const auto json
        = nlohmann::json::parse(R"({"processing": {"texturemaps": {"Textures/Armor/Helmet.DDS": ")" + type + R"("}}})");
    PGParams params;
    params.readJSON(json);
    ASSERT_EQ(params.processing.textureMaps.size(), 1U);
    EXPECT_EQ(params.processing.textureMaps[0].first, L"textures\\armor\\helmet.dds");
    EXPECT_EQ(params.processing.textureMaps[0].second, PGEnums::TextureType::Diffuse);
}

TEST(PGParamsTest,
     NormalizedRulesAreSpelledAsTheFileReadSpellsThem)
{
    PGParams params;
    params.processing.allowList = { L"meshes/a/*" };
    params.processing.blockList = { L"Meshes/B/*.nif" };
    params.processing.textureMaps = { { L"Textures/A/Manual.DDS", PGEnums::TextureType::ComplexMaterial } };
    params.normalizeRules();
    EXPECT_EQ(params.processing.allowList, std::vector<std::wstring> { L"meshes\\a\\*" });
    EXPECT_EQ(params.processing.blockList, std::vector<std::wstring> { L"Meshes\\B\\*.nif" });
    ASSERT_EQ(params.processing.textureMaps.size(), 1U);
    EXPECT_EQ(params.processing.textureMaps[0].first, L"textures\\a\\manual.dds");
    EXPECT_EQ(params.processing.textureMaps[0].second, PGEnums::TextureType::ComplexMaterial);
}

TEST(PGParamsTest,
     TwoSpellingsOfOneRuleAreADuplicateOnceNormalized)
{
    PGParams params;
    params.processing.blockList = { L"meshes/a/*", L"meshes\\a\\*" };
    EXPECT_TRUE(params.validate().empty());

    params.normalizeRules();
    const auto problems = params.validate();
    ASSERT_EQ(problems.size(), 1U);
    EXPECT_EQ(problems[0].key, "launcher.validation.meshBlockListDuplicate");
    EXPECT_EQ(problems[0].item, L"meshes\\a\\*");
}

}
