#include "patchers/PatcherMeshGlobalParticleLightsToLP.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"

#include "Animation.hpp"
#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "Keys.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Object3d.hpp"
#include "Particles.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

using TestUtil::NIFBuilder;

constexpr uint16_t particleLightAlphaFlags = 4109;

/**
 * @brief A data folder with a lamp mesh under construction, mapped by the global PGDirectory, which also provides the
 * output folder the Light Placer JSON is written to.
 */
class PatcherMeshGlobalParticleLightsToLPTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    NIFBuilder m_builder;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\lamp.nif";

    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }
    [[nodiscard]] std::filesystem::path outputJSON() const { return outDir() / L"LightPlacer\\parallaxgen.json"; }

    void SetUp() override
    {
        std::filesystem::create_directories(m_dir / L"Data");
        m_pgd = std::make_unique<PGDirectory>(m_dir / L"Data", outDir());
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());
    }

    void TearDown() override
    {
        // Writes (and forgets) whatever the test collected.
        PatcherMeshGlobalParticleLightsToLP::finalize();
        PGGlobals::setPGD(nullptr);
    }

    /**
     * @brief Adds a particle light: a billboard node at the position with one effect shader shape whose vertices are
     * all at the given distance and carry the given vertex color.
     */
    nifly::NiBillboardNode* addParticleLight(const std::string& name,
                                             const nifly::Vector3& position,
                                             float distance,
                                             const std::array<uint8_t,
                                                              4>& vertexColor,
                                             const nifly::Color4& baseColor = { 1,
                                                                                1,
                                                                                1,
                                                                                1 })
    {
        auto* node = m_builder.addBillboardNode(name);
        nifly::MatTransform transform;
        transform.translation = position;
        node->SetTransformToParent(transform);

        auto* shape = m_builder.addEffectShape(name + "Shape", "textures\\fx.dds", node);
        shape->SetVertexColors(true);
        shape->vertData[0].vert = nifly::Vector3(distance, 0, 0);
        shape->vertData[1].vert = nifly::Vector3(0, distance, 0);
        shape->vertData[2].vert = nifly::Vector3(0, 0, distance);
        shape->vertData[3].vert = nifly::Vector3(-distance, 0, 0);
        for (auto& vertex : shape->vertData)
            std::ranges::copy(vertexColor, std::begin(vertex.colorData));
        m_builder.addAlphaProperty(shape, particleLightAlphaFlags);

        auto* shader = dynamic_cast<nifly::BSEffectShaderProperty*>(m_builder.nif().GetShader(shape));
        shader->shaderFlags1 |= nifly::SLSF1_SOFT_EFFECT;
        shader->baseColor = baseColor;
        shader->baseColorScale = 0.5F;
        return node;
    }

    [[nodiscard]] nifly::BSEffectShaderProperty* effectShaderOf(nifly::NiBillboardNode* node)
    {
        std::vector<uint32_t> children;
        node->childRefs.GetIndices(children);
        auto* shape = m_builder.nif().GetHeader().GetBlock<nifly::NiShape>(children.at(0));
        return dynamic_cast<nifly::BSEffectShaderProperty*>(m_builder.nif().GetShader(shape));
    }

    /// @brief Attaches an emissive multiple controller with a linear two key animation to the effect shader.
    void addFadeController(nifly::BSEffectShaderProperty* shader,
                           uint32_t controlledVariable = 0,
                           nifly::NiKeyType keyType = nifly::LINEAR_KEY)
    {
        auto& header = m_builder.nif().GetHeader();
        auto data = std::make_unique<nifly::NiFloatData>();
        data->data.SetInterpolationType(keyType);
        nifly::NiAnimationKey<float> key;
        key.type = keyType;
        key.time = 0;
        key.value = 1;
        data->data.AddKey(key);
        key.time = 0.5F;
        key.value = 0.25F;
        data->data.AddKey(key);
        auto interpolator = std::make_unique<nifly::NiFloatInterpolator>();
        interpolator->dataRef.index = header.AddBlock(std::move(data));
        auto controller = std::make_unique<nifly::BSEffectShaderPropertyFloatController>();
        controller->typeOfControlledVariable = controlledVariable;
        controller->interpolatorRef.index = header.AddBlock(std::move(interpolator));
        shader->controllerRef.index = header.AddBlock(std::move(controller));
    }

    /// @brief Chains a base color controller behind the controllers of the effect shader.
    void addColorController(nifly::BSEffectShaderProperty* shader)
    {
        auto& header = m_builder.nif().GetHeader();
        auto data = std::make_unique<nifly::NiPosData>();
        data->data.SetInterpolationType(nifly::LINEAR_KEY);
        nifly::NiAnimationKey<nifly::Vector3> key;
        key.type = nifly::LINEAR_KEY;
        key.value = nifly::Vector3(1, 0, 0);
        data->data.AddKey(key);
        auto interpolator = std::make_unique<nifly::NiPoint3Interpolator>();
        interpolator->dataRef.index = header.AddBlock(std::move(data));
        auto controller = std::make_unique<nifly::BSEffectShaderPropertyColorController>();
        controller->interpolatorRef.index = header.AddBlock(std::move(interpolator));
        const auto controllerID = header.AddBlock(std::move(controller));

        nifly::NiBlockRef<nifly::NiTimeController>* last = &shader->controllerRef;
        while (!last->IsEmpty())
            last = &header.GetBlock<nifly::NiTimeController>(last->index)->nextControllerRef;
        last->index = controllerID;
    }

    [[nodiscard]] nlohmann::json writtenJSON() const
    {
        PatcherMeshGlobalParticleLightsToLP::finalize();
        return nlohmann::json::parse(TestUtil::readFile(outputJSON()));
    }
};

TEST_F(PatcherMeshGlobalParticleLightsToLPTest,
       ParticleLightsBecomeLightPlacerEntries)
{
    auto* light = addParticleLight("Light", nifly::Vector3(10, 20, 30), 5, { 255, 128, 0, 255 });
    addFadeController(effectShaderOf(light));
    addColorController(effectShaderOf(light)); // Light Placer gets no color animation
    // The same light somewhere else, which merges into the same entry.
    auto* twin = addParticleLight("Twin", nifly::Vector3(-1.234F, 0, 0), 5, { 255, 128, 0, 255 });
    addFadeController(effectShaderOf(twin));
    auto* regular = m_builder.addShape("Regular");
    const auto numShapesBefore = m_builder.nif().GetShapes().size();

    PatcherMeshGlobalParticleLightsToLP patcher(meshPath, &m_builder.nif());
    EXPECT_EQ(patcher.patcherName(), "ParticleLightsToLP");
    EXPECT_TRUE(patcher.applyPatch());

    // The particle lights are gone from the mesh, the regular shape stays.
    EXPECT_EQ(m_builder.nif().GetShapes().size(), numShapesBefore - 2);
    EXPECT_EQ(m_builder.nif().GetShapes()[0], regular);
    EXPECT_NE(m_builder.nif().GetBlockID(regular), nifly::NIF_NPOS);

    const auto json = writtenJSON();
    ASSERT_TRUE(json.is_array());
    ASSERT_EQ(json.size(), 1U);
    EXPECT_EQ(json[0]["models"], nlohmann::json::array({ "a\\lamp.nif" }));
    ASSERT_EQ(json[0]["lights"].size(), 1U);
    const auto& entry = json[0]["lights"][0];
    EXPECT_EQ(entry["points"], nlohmann::json::array({ { 10, 20, 30 }, { -1.23, 0, 0 } }));
    EXPECT_EQ(entry["data"]["light"], "MagicLightWhite01");
    EXPECT_EQ(entry["data"]["color"], nlohmann::json::array({ 255, 128, 0 })); // from the vertex colors
    EXPECT_EQ(entry["data"]["radius"], 5);
    EXPECT_EQ(entry["data"]["fade"], 0.5);
    EXPECT_EQ(entry["data"]["fadeController"]["interpolation"], "Linear");
    EXPECT_EQ(entry["data"]["fadeController"]["keys"],
              nlohmann::json::array({ { { "time", 0 }, { "value", 1 }, { "forward", 0 }, { "backward", 0 } },
                                      { { "time", 0.5 }, { "value", 0.25 }, { "forward", 0 }, { "backward", 0 } } }));
    EXPECT_FALSE(entry["data"].contains("colorController"));

    // Everything was written, so finalizing again writes nothing new.
    std::filesystem::remove(outputJSON());
    PatcherMeshGlobalParticleLightsToLP::finalize();
    EXPECT_FALSE(std::filesystem::exists(outputJSON()));
}

TEST_F(PatcherMeshGlobalParticleLightsToLPTest,
       TheShaderColorIsUsedUnlessItIsWhite)
{
    auto* light
        = addParticleLight("Light", nifly::Vector3(0, 0, 0), 2, { 255, 128, 0, 255 }, nifly::Color4(0.5F, 0.25F, 1, 1));
    addFadeController(effectShaderOf(light), 1); // not the emissive multiple, so no fade controller
    auto* step
        = addParticleLight("Step", nifly::Vector3(1, 1, 1), 2, { 0, 0, 0, 255 }, nifly::Color4(0.5F, 0.25F, 1, 1));
    addFadeController(effectShaderOf(step), 0, nifly::NO_INTERP);

    PatcherMeshGlobalParticleLightsToLP patcher(meshPath, &m_builder.nif());
    EXPECT_TRUE(patcher.applyPatch());

    const auto json = writtenJSON();
    ASSERT_EQ(json.size(), 1U);
    ASSERT_EQ(json[0]["lights"].size(), 2U);
    for (const auto& entry : json[0]["lights"]) {
        EXPECT_EQ(entry["data"]["color"], nlohmann::json::array({ 127, 63, 255 }));
        EXPECT_EQ(entry["data"]["radius"], 2);
        if (entry["data"].contains("fadeController"))
            EXPECT_EQ(entry["data"]["fadeController"]["interpolation"], "Step");
    }
}

TEST_F(PatcherMeshGlobalParticleLightsToLPTest,
       OnlyAdditiveSoftEffectBillboardsCount)
{
    auto* wrongAlpha = addParticleLight("WrongAlpha", nifly::Vector3(0, 0, 0), 1, { 255, 255, 255, 255 });
    std::vector<uint32_t> children;
    wrongAlpha->childRefs.GetIndices(children);
    auto* wrongAlphaShape = m_builder.nif().GetHeader().GetBlock<nifly::NiShape>(children.at(0));
    m_builder.nif().GetHeader().GetBlock(wrongAlphaShape->AlphaPropertyRef())->flags = 4844;
    auto* notSoft = addParticleLight("NotSoft", nifly::Vector3(0, 0, 0), 1, { 255, 255, 255, 255 });
    effectShaderOf(notSoft)->shaderFlags1 &= ~static_cast<uint32_t>(nifly::SLSF1_SOFT_EFFECT);
    auto* noVertices = addParticleLight("NoVertices", nifly::Vector3(0, 0, 0), 1, { 255, 255, 255, 255 });
    children.clear(); // nifly appends to the list
    noVertices->childRefs.GetIndices(children);
    m_builder.nif().GetHeader().GetBlock<nifly::BSTriShape>(children.at(0))->vertData.clear();
    m_builder.addBillboardNode("Empty");
    auto* noShader = m_builder.addBillboardNode("NoShader");
    m_builder.addAlphaProperty(m_builder.addShaderlessShape("Bare", noShader), particleLightAlphaFlags);
    // A regular node with a valid light setup is not a billboard.
    auto* node = m_builder.addNode("Node");
    auto* nodeShape = m_builder.addEffectShape("NodeShape", "textures\\fx.dds", node);
    m_builder.addAlphaProperty(nodeShape, particleLightAlphaFlags);
    dynamic_cast<nifly::BSEffectShaderProperty*>(m_builder.nif().GetShader(nodeShape))->shaderFlags1
        |= nifly::SLSF1_SOFT_EFFECT;
    const auto numBlocks = m_builder.nif().GetHeader().GetNumBlocks();

    PatcherMeshGlobalParticleLightsToLP patcher(meshPath, &m_builder.nif());
    EXPECT_FALSE(patcher.applyPatch());
    EXPECT_EQ(m_builder.nif().GetHeader().GetNumBlocks(), numBlocks);
    PatcherMeshGlobalParticleLightsToLP::finalize();
    EXPECT_FALSE(std::filesystem::exists(outputJSON()));

    const auto fromFactory = PatcherMeshGlobalParticleLightsToLP::factory()(meshPath, &m_builder.nif());
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "ParticleLightsToLP");
}

TEST_F(PatcherMeshGlobalParticleLightsToLPTest,
       ParticleSystemsProvideTheShaderAndAlphaOfTheirBillboard)
{
    auto* node = m_builder.addBillboardNode("Particles");
    nifly::MatTransform transform;
    transform.translation = nifly::Vector3(1, 2, 3);
    node->SetTransformToParent(transform);
    auto* shape = m_builder.addShaderlessShape("Geometry", node);
    for (auto& vertex : shape->vertData)
        vertex.vert = nifly::Vector3(0, 0, 3);
    auto* particles = m_builder.addParticleSystem("System", node);

    auto& header = m_builder.nif().GetHeader();
    auto alpha = std::make_unique<nifly::NiAlphaProperty>();
    alpha->flags = particleLightAlphaFlags;
    particles->alphaPropertyRef.index = header.AddBlock(std::move(alpha));
    auto shader = std::make_unique<nifly::BSEffectShaderProperty>();
    shader->shaderFlags1 |= nifly::SLSF1_SOFT_EFFECT;
    shader->baseColor = nifly::Color4(0, 1, 0, 1);
    shader->baseColorScale = 2;
    particles->shaderPropertyRef.index = header.AddBlock(std::move(shader));

    PatcherMeshGlobalParticleLightsToLP patcher(meshPath, &m_builder.nif());
    EXPECT_TRUE(patcher.applyPatch());
    EXPECT_TRUE(m_builder.nif().GetShapes().empty());

    const auto json = writtenJSON();
    ASSERT_EQ(json.size(), 1U);
    const auto& entry = json[0]["lights"][0];
    EXPECT_EQ(entry["points"], nlohmann::json::array({ { 1, 2, 3 } }));
    EXPECT_EQ(entry["data"]["color"], nlohmann::json::array({ 0, 255, 0 }));
    EXPECT_EQ(entry["data"]["radius"], 3);
    EXPECT_EQ(entry["data"]["fade"], 2);
    EXPECT_FALSE(entry["data"].contains("fadeController"));
}

}
