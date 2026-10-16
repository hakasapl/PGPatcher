#include "patchers/PatcherMeshPreStockMarker.hpp"

#include "NIFBuilder.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGMaterialState.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "ExtraData.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <spdlog/common.h>

#include <string>

namespace {

namespace TestLog = TestUtil::TestLog;
using PGEnums::TextureSlots;
using TestUtil::NIFBuilder;
using Marker = PatcherMeshPreStockMarker;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    return set;
}

/**
 * @brief A mesh with one default shader shape in its stock state.
 */
class PatcherMeshPreStockMarkerTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    NIFBuilder m_builder;
    nifly::BSTriShape* m_shape = nullptr;
    nifly::BSLightingShaderProperty* m_shader = nullptr;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";

    void SetUp() override
    {
        m_shape = m_builder.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
        m_shader = m_builder.lightingShader(m_shape);
    }

    void TearDown() override { Marker::loadOptions(false); }

    [[nodiscard]] nifly::NifFile& nif() { return m_builder.nif(); }
};

TEST_F(PatcherMeshPreStockMarkerTest,
       CaptureAndDiffDescribeTheShaderState)
{
    const auto stock = Marker::captureStock(nif(), *m_shape);
    EXPECT_EQ(stock["version"], Marker::formatVersion);
    EXPECT_EQ(stock["texture_count"], 9U);
    EXPECT_EQ(stock["textures"]["0"], "textures\\a\\stone.dds");
    EXPECT_EQ(stock["textures"]["1"], "textures\\a\\stone_n.dds");
    EXPECT_EQ(stock["textures"]["2"], "");
    EXPECT_EQ(stock["shader"], PGMaterialState::shaderToJSON(*m_shader));
    EXPECT_TRUE(Marker::isEmptyStock(Marker::diffStock(nif(), *m_shape, stock)));

    // Only what differs from the stock state is kept, with the stock values.
    const auto stockGlossiness = stock["shader"]["glossiness"];
    m_shader->glossiness = 99;
    PGNIFUtil::setTextureSlot(&nif(), m_shape, TextureSlots::Diffuse, std::string("textures\\a\\patched.dds"));
    m_builder.textureSet(m_shape)->textures.resize(6);
    const auto differences = Marker::diffStock(nif(), *m_shape, stock);
    EXPECT_FALSE(Marker::isEmptyStock(differences));
    EXPECT_EQ(differences["version"], Marker::formatVersion);
    EXPECT_EQ(differences["textures"], nlohmann::json({ { "0", "textures\\a\\stone.dds" } }));
    EXPECT_EQ(differences["shader"], nlohmann::json({ { "glossiness", stockGlossiness } }));
    EXPECT_EQ(differences["texture_count"], 9U);

    // Shapes without a lighting shader only have a version.
    auto* effect = m_builder.addEffectShape("Glow");
    EXPECT_EQ(Marker::captureStock(nif(), *effect), nlohmann::json({ { "version", Marker::formatVersion } }));
    EXPECT_TRUE(Marker::isEmptyStock(Marker::diffStock(nif(), *effect, stock)));

    EXPECT_TRUE(Marker::isEmptyStock(nlohmann::json::object()));
    EXPECT_TRUE(Marker::isEmptyStock(nlohmann::json::array({ 1 })));
    EXPECT_TRUE(Marker::isEmptyStock(nlohmann::json({ { "version", 1 } })));
    EXPECT_FALSE(Marker::isEmptyStock(nlohmann::json({ { "version", 1 }, { "shader", nlohmann::json::object() } })));
}

TEST_F(PatcherMeshPreStockMarkerTest,
       MarkersAreFoundWrittenAndRemoved)
{
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), nullptr);
    const auto stock = Marker::captureStock(nif(), *m_shape);
    const auto numBlocks = nif().GetHeader().GetNumBlocks();

    Marker::writeMarker(nif(), *m_shape, stock);
    auto* marker = Marker::findMarker(nif(), *m_shape);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(marker->name.get(), Marker::extraDataName);
    EXPECT_EQ(nlohmann::json::parse(marker->stringData.get()), stock);
    EXPECT_EQ(nif().GetHeader().GetNumBlocks(), numBlocks + 1);

    // Writing again replaces the text instead of adding a block.
    Marker::writeMarker(nif(), *m_shape, nlohmann::json({ { "version", 1 } }));
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), marker);
    EXPECT_EQ(nif().GetHeader().GetNumBlocks(), numBlocks + 1);

    // Other extra data is not mistaken for a marker.
    auto* other = m_builder.addShape("Other", slots(L"textures\\a\\stone.dds"));
    m_builder.addStringExtraData(other, "PG_OTHER", "{}");
    EXPECT_EQ(Marker::findMarker(nif(), *other), nullptr);

    // Empty markers are deleted, the others stay.
    marker->stringData.get().clear();
    Marker::writeMarker(nif(), *other, stock);
    const auto before = nif().GetHeader().GetNumBlocks();
    Marker::removeEmptyMarkers(nif());
    EXPECT_EQ(nif().GetHeader().GetNumBlocks(), before - 1);
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), nullptr);
    ASSERT_NE(Marker::findMarker(nif(), *other), nullptr);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *other)->stringData.get()), stock);
    Marker::removeEmptyMarkers(nif()); // nothing left to remove
    EXPECT_EQ(nif().GetHeader().GetNumBlocks(), before - 1);
}

TEST_F(PatcherMeshPreStockMarkerTest,
       ApplyPatchRevertsAPatchedShapeToStock)
{
    Marker patcher(meshPath, &nif());
    EXPECT_EQ(patcher.patcherName(), "StockMarker");

    // Without a marker a regular mesh is left alone.
    auto current = PGNIFUtil::textureSlots(&nif(), m_shape);
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape));
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), nullptr);

    // A patched shape with the block a patch run leaves behind.
    const auto stock = Marker::captureStock(nif(), *m_shape);
    const auto stockGlossiness = stock["shader"]["glossiness"].get<float>();
    m_shader->glossiness = 99;
    m_shader->SetShaderType(nifly::BSLSP_ENVMAP);
    PGNIFUtil::setShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING);
    PGNIFUtil::setTextureSlot(&nif(), m_shape, TextureSlots::Diffuse, std::string("textures\\a\\patched.dds"));
    PGNIFUtil::setTextureSlot(&nif(), m_shape, TextureSlots::EnvMask, std::string("textures\\a\\stone_m.dds"));
    Marker::writeMarker(nif(), *m_shape, Marker::diffStock(nif(), *m_shape, stock));

    current = PGNIFUtil::textureSlots(&nif(), m_shape);
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->glossiness, stockGlossiness);
    EXPECT_EQ(m_shader->GetShaderType(), nifly::BSLSP_DEFAULT);
    EXPECT_FALSE(PGNIFUtil::hasShaderFlag(m_shader, nifly::SLSF1_ENVIRONMENT_MAPPING));
    EXPECT_EQ(current, slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    EXPECT_EQ(PGNIFUtil::textureSlots(&nif(), m_shape), current);
    // The block holds the complete stock state until it is trimmed again.
    ASSERT_NE(Marker::findMarker(nif(), *m_shape), nullptr);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *m_shape)->stringData.get()), stock);
    EXPECT_EQ(Marker::captureStock(nif(), *m_shape), stock);

    // Nothing changed since, so the block is trimmed to nothing and removed.
    Marker::finalizeMarker(nif(), *m_shape);
    EXPECT_TRUE(Marker::findMarker(nif(), *m_shape)->stringData.get().empty());
    Marker::removeEmptyMarkers(nif());
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), nullptr);
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape));

    const auto fromFactory = Marker::factory()(meshPath, &nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "StockMarker");
}

TEST_F(PatcherMeshPreStockMarkerTest,
       ApplyPatchKeepsAlternateTexturesAndRejectsBrokenMarkers)
{
    Marker patcher(meshPath, &nif());
    const auto stock = Marker::captureStock(nif(), *m_shape);
    m_shader->glossiness = 99;
    PGNIFUtil::setTextureSlot(&nif(), m_shape, TextureSlots::Diffuse, std::string("textures\\a\\patched.dds"));
    Marker::writeMarker(nif(), *m_shape, Marker::diffStock(nif(), *m_shape, stock));

    // The slots of an alternate texture do not mirror the shape, so only the shader is restored.
    auto altTex = slots(L"textures\\a\\alt.dds");
    EXPECT_TRUE(patcher.applyPatch(altTex, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->glossiness, stock["shader"]["glossiness"].get<float>());
    EXPECT_EQ(altTex, slots(L"textures\\a\\alt.dds"));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif(), m_shape, TextureSlots::Diffuse), "textures\\a\\patched.dds");

    // Broken blocks are reported and ignored, and finalizing leaves them as they are.
    auto* invalid = m_builder.addShape("Invalid", slots(L"textures\\a\\stone.dds"));
    auto current = PGNIFUtil::textureSlots(&nif(), invalid);
    auto* marker = m_builder.addStringExtraData(invalid, Marker::extraDataName, "");
    const auto expectRejected = [&](const std::string& text, const std::string& reason) {
        TestLog::clear();
        marker->stringData.get() = text;
        EXPECT_FALSE(patcher.applyPatch(current, *invalid)) << text;
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, "Ignoring the PG_STOCK block of shape \"Invalid\": "))
            << reason;
        EXPECT_TRUE(TestLog::contains(spdlog::level::warn, reason)) << reason;
        Marker::finalizeMarker(nif(), *invalid);
        EXPECT_EQ(marker->stringData.get(), text);
    };
    expectRejected("", "the block is empty");
    expectRejected("{not json", "syntax error");
    expectRejected("[1, 2]", "the block is not a JSON object");
    expectRejected(R"({"version": "one"})", "the version is not a number");
    expectRejected(R"({"version": 99})", "version 99 is newer than this version of PGPatcher supports");
    expectRejected(R"({"version": 18446744073709551615})", "is newer than this version of PGPatcher supports");

    // A block without a version is taken as the current version.
    marker->stringData.get() = R"({"shader": {"glossiness": 3}})";
    EXPECT_TRUE(patcher.applyPatch(current, *invalid));
    EXPECT_FLOAT_EQ(m_builder.lightingShader(invalid)->glossiness, 3);

    // Shapes without a lighting shader are skipped.
    auto* effect = m_builder.addEffectShape("Glow");
    EXPECT_FALSE(patcher.applyPatch(current, *effect));
}

TEST_F(PatcherMeshPreStockMarkerTest,
       BodySlideShapeDataGetsAMarkerThatIsTrimmedAfterPatching)
{
    Marker patcher(meshPath, &nif());
    PGPlugin::MeshUseAttributes use;
    use.isBodySlideShapeData = true;
    use.isDummyUse = true;
    patcher.setMeshUse(use);

    // The block is bookkeeping, not a change to the shape.
    auto current = PGNIFUtil::textureSlots(&nif(), m_shape);
    const auto stock = Marker::captureStock(nif(), *m_shape);
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape));
    auto* marker = Marker::findMarker(nif(), *m_shape);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(nlohmann::json::parse(marker->stringData.get()), stock);

    // After the other patchers ran, only their changes are kept.
    m_shader->glossiness = 99;
    Marker::finalizeMarker(nif(), *m_shape);
    EXPECT_EQ(nlohmann::json::parse(marker->stringData.get()),
              nlohmann::json({ { "version", Marker::formatVersion },
                               { "shader", { { "glossiness", stock["shader"]["glossiness"] } } } }));
    Marker::removeEmptyMarkers(nif());
    EXPECT_NE(Marker::findMarker(nif(), *m_shape), nullptr);

    // A mesh built from it is reverted when it is patched later.
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    EXPECT_FLOAT_EQ(m_shader->glossiness, stock["shader"]["glossiness"].get<float>());

    // Shapes without a lighting shader get no block.
    auto* effect = m_builder.addEffectShape("Glow");
    EXPECT_FALSE(patcher.applyPatch(current, *effect));
    EXPECT_EQ(Marker::findMarker(nif(), *effect), nullptr);
}

TEST_F(PatcherMeshPreStockMarkerTest,
       EveryPatchedShapeGetsAMarkerInPGLiteMode)
{
    Marker patcher(meshPath, &nif());
    PGPlugin::MeshUseAttributes use;
    use.recType = PGPlugin::ModelRecordType::StaticObject;
    patcher.setMeshUse(use);
    auto current = PGNIFUtil::textureSlots(&nif(), m_shape);

    // A plugin use gets no block by default.
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape));
    EXPECT_EQ(Marker::findMarker(nif(), *m_shape), nullptr);

    // In PGLite mode every patched shape gets one, trimmed to the changes as for BodySlide ShapeData.
    Marker::loadOptions(true);
    const auto stock = Marker::captureStock(nif(), *m_shape);
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape));
    auto* marker = Marker::findMarker(nif(), *m_shape);
    ASSERT_NE(marker, nullptr);
    EXPECT_EQ(nlohmann::json::parse(marker->stringData.get()), stock);
    m_shader->glossiness = 99;
    Marker::finalizeMarker(nif(), *m_shape);
    EXPECT_EQ(nlohmann::json::parse(marker->stringData.get()),
              nlohmann::json({ { "version", Marker::formatVersion },
                               { "shader", { { "glossiness", stock["shader"]["glossiness"] } } } }));

    // The option is the run's: off again, a shape gets no block.
    Marker::loadOptions(false);
    auto* other = m_builder.addShape("Other", slots(L"textures\\a\\other.dds"));
    EXPECT_FALSE(patcher.applyPatch(current, *other));
    EXPECT_EQ(Marker::findMarker(nif(), *other), nullptr);
}

TEST_F(PatcherMeshPreStockMarkerTest,
       MarkPatchedShapesRecordsTheDifferencesBetweenTwoMeshes)
{
    NIFBuilder stock;
    stock.addShape("A", slots(L"textures\\a\\stone.dds"));
    stock.addShape("B", slots(L"textures\\a\\stone.dds"));
    stock.addShape("Gone", slots(L"textures\\a\\stone.dds"));
    stock.addShape("Dup", slots(L"textures\\a\\stone.dds"));
    stock.addShape("Dup", slots(L"textures\\a\\stone.dds"));

    NIFBuilder patched;
    auto* a = patched.addShape("A", slots(L"textures\\a\\stone.dds"));
    auto* b = patched.addShape("B", slots(L"textures\\a\\stone.dds"));
    patched.addShape("Extra", slots(L"textures\\a\\stone.dds"));
    auto* dup1 = patched.addShape("Dup", slots(L"textures\\a\\stone.dds"));
    auto* dup2 = patched.addShape("Dup", slots(L"textures\\a\\stone.dds"));
    const auto stockGlossiness = patched.lightingShader(a)->glossiness;
    patched.lightingShader(a)->glossiness = 99;
    patched.lightingShader(dup2)->specularStrength = 7;
    // A stale block from an earlier run on a shape that is at stock now.
    patched.addStringExtraData(b, Marker::extraDataName, R"({"version": 1, "shader": {"glossiness": 5}})");

    EXPECT_EQ(Marker::markPatchedShapes(stock.nif(), patched.nif(), L"meshes\\x.nif"), 2U);

    ASSERT_NE(Marker::findMarker(patched.nif(), *a), nullptr);
    EXPECT_EQ(
        nlohmann::json::parse(Marker::findMarker(patched.nif(), *a)->stringData.get()),
        nlohmann::json({ { "version", Marker::formatVersion }, { "shader", { { "glossiness", stockGlossiness } } } }));
    EXPECT_EQ(Marker::findMarker(patched.nif(), *b), nullptr);
    EXPECT_EQ(Marker::findMarker(patched.nif(), *dup1), nullptr);
    ASSERT_NE(Marker::findMarker(patched.nif(), *dup2), nullptr);
    EXPECT_TRUE(nlohmann::json::parse(Marker::findMarker(patched.nif(), *dup2)->stringData.get())["shader"].contains(
        "specular_strength"));

    EXPECT_TRUE(TestLog::contains(spdlog::level::warn,
                                  "meshes\\x.nif: shape \"Gone\" only exists in the original mesh, a deleted shape "
                                  "cannot be restored"));
    EXPECT_TRUE(TestLog::contains(
        spdlog::level::warn,
        "meshes\\x.nif: shape \"Extra\" only exists in the patched mesh, nothing to compare it with"));
}

TEST_F(PatcherMeshPreStockMarkerTest,
       AlphaPropertyIsRecordedAndRestored)
{
    Marker patcher(meshPath, &nif());

    // A shape without an alpha property records null, one with it records its fields.
    auto stock = Marker::captureStock(nif(), *m_shape);
    ASSERT_TRUE(stock.contains("alpha_property"));
    EXPECT_TRUE(stock["alpha_property"].is_null());
    auto* alpha = m_builder.addAlphaProperty(m_shape, 4845, 64);
    stock = Marker::captureStock(nif(), *m_shape);
    EXPECT_EQ(stock["alpha_property"], nlohmann::json({ { "flags", 4845 }, { "threshold", 64 } }));
    EXPECT_TRUE(Marker::isEmptyStock(Marker::diffStock(nif(), *m_shape, stock)));

    // Only the fields a patch changed are kept, with their stock values.
    alpha->threshold = 200;
    auto differences = Marker::diffStock(nif(), *m_shape, stock);
    EXPECT_EQ(differences["alpha_property"], nlohmann::json({ { "threshold", 64 } }));
    EXPECT_FALSE(differences.contains("textures"));

    // The patched shape is reverted, and the block holds the complete stock state until it is trimmed again.
    Marker::writeMarker(nif(), *m_shape, differences);
    auto current = PGNIFUtil::textureSlots(&nif(), m_shape);
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    EXPECT_EQ(alpha->threshold, 64U);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *m_shape)->stringData.get()), stock);
    EXPECT_FALSE(patcher.applyPatch(current, *m_shape)); // nothing left to revert

    // A patch that removed the alpha property: the whole stock property is recorded and put back.
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif(), *m_shape));
    differences = Marker::diffStock(nif(), *m_shape, stock);
    EXPECT_EQ(differences["alpha_property"], stock["alpha_property"]);
    Marker::writeMarker(nif(), *m_shape, differences);
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    alpha = nif().GetAlphaProperty(m_shape);
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->flags, 4845U);
    EXPECT_EQ(alpha->threshold, 64U);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *m_shape)->stringData.get()), stock);

    // A recorded object with fields left out restores the defaults for them.
    EXPECT_TRUE(PGNIFUtil::deleteAlphaProperty(nif(), *m_shape));
    Marker::writeMarker(nif(), *m_shape, nlohmann::json({ { "alpha_property", { { "threshold", 32 } } } }));
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    alpha = nif().GetAlphaProperty(m_shape);
    ASSERT_NE(alpha, nullptr);
    EXPECT_EQ(alpha->flags, 4844U);
    EXPECT_EQ(alpha->threshold, 32U);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *m_shape)->stringData.get())["alpha_property"],
              nlohmann::json({ { "flags", 4844 }, { "threshold", 32 } }));

    // A patch that added an alpha property to a shape without one: null is recorded and removes it again.
    auto* plain = m_builder.addShape("Plain", slots(L"textures\\a\\stone.dds"));
    const auto plainStock = Marker::captureStock(nif(), *plain);
    m_builder.addAlphaProperty(plain);
    differences = Marker::diffStock(nif(), *plain, plainStock);
    ASSERT_TRUE(differences.contains("alpha_property"));
    EXPECT_TRUE(differences["alpha_property"].is_null());
    Marker::writeMarker(nif(), *plain, differences);
    auto plainSlots = PGNIFUtil::textureSlots(&nif(), plain);
    EXPECT_TRUE(patcher.applyPatch(plainSlots, *plain));
    EXPECT_EQ(nif().GetAlphaProperty(plain), nullptr);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(nif(), *plain)->stringData.get()), plainStock);

    // A malformed value is ignored.
    Marker::writeMarker(nif(), *plain, nlohmann::json({ { "alpha_property", "opaque" } }));
    EXPECT_FALSE(patcher.applyPatch(plainSlots, *plain));
    EXPECT_EQ(nif().GetAlphaProperty(plain), nullptr);

    // gendiffblocks records the alpha property as well.
    NIFBuilder patched;
    auto* patchedShape = patched.addShape("Stone", slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds"));
    patched.addAlphaProperty(patchedShape, 4845, 64);
    EXPECT_EQ(Marker::markPatchedShapes(nif(), patched.nif(), L"meshes\\x.nif"), 1U);
    ASSERT_NE(Marker::findMarker(patched.nif(), *patchedShape), nullptr);
    EXPECT_EQ(nlohmann::json::parse(Marker::findMarker(patched.nif(), *patchedShape)->stringData.get()),
              nlohmann::json({ { "version", Marker::formatVersion },
                               { "alpha_property", { { "flags", 4844 }, { "threshold", 32 } } } }));
}

TEST_F(PatcherMeshPreStockMarkerTest,
       RestoringASharedTextureSetCopiesItForTheMarkedShape)
{
    Marker patcher(meshPath, &nif());

    // A second shape shares the texture set of the first one, as vanilla meshes do.
    auto* sharer = m_builder.addShape("Sharer");
    const auto sharedSet = m_shader->textureSetRef.index;
    m_builder.lightingShader(sharer)->textureSetRef.index = sharedSet;

    // Both shapes were patched to another diffuse through the shared block; only the first one carries a block.
    const auto stock = Marker::captureStock(nif(), *m_shape);
    m_builder.textureSet(m_shape)->textures[0].get() = "textures\\a\\patched.dds";
    Marker::writeMarker(nif(), *m_shape, Marker::diffStock(nif(), *m_shape, stock));
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif(), sharer, TextureSlots::Diffuse), "textures\\a\\patched.dds");

    // The restore gives the marked shape its own set with the stock textures and leaves the shared block alone.
    auto current = PGNIFUtil::textureSlots(&nif(), m_shape);
    const auto numBlocks = nif().GetHeader().GetNumBlocks();
    EXPECT_TRUE(patcher.applyPatch(current, *m_shape));
    EXPECT_EQ(nif().GetHeader().GetNumBlocks(), numBlocks + 1);
    EXPECT_NE(m_shader->textureSetRef.index, sharedSet);
    EXPECT_EQ(m_builder.lightingShader(sharer)->textureSetRef.index, sharedSet);
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif(), m_shape, TextureSlots::Diffuse), "textures\\a\\stone.dds");
    EXPECT_EQ(current[0], L"textures\\a\\stone.dds");
    EXPECT_EQ(PGNIFUtil::textureSlot(&nif(), sharer, TextureSlots::Diffuse), "textures\\a\\patched.dds");
}
}
