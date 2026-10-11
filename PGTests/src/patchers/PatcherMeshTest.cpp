#include "patchers/base/PatcherMesh.hpp"

#include "NIFBuilder.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGPlugin.hpp"
#include "TestUtil.hpp"
#include "patchers/PatcherMeshShaderDefault.hpp"
#include "patchers/PatcherMeshShaderTransformParallaxToCM.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/base/PatcherMeshShader.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "BasicTypes.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {

using PatcherMatch = PatcherMeshShader::PatcherMatch;
using TestUtil::NIFBuilder;

PGTypes::TextureSet slots(const std::wstring& diffuse,
                          const std::wstring& normal = L"")
{
    PGTypes::TextureSet set;
    set[0] = diffuse;
    set[1] = normal;
    return set;
}

/// @brief Exposes the protected state of a mesh patcher.
class ExposedPatcher : public PatcherMeshShaderDefault {
public:
    using PatcherMeshShaderDefault::meshUse;
    using PatcherMeshShaderDefault::nif;
    using PatcherMeshShaderDefault::nifPath;
    using PatcherMeshShaderDefault::PatcherMeshShaderDefault;
    using PatcherMeshShaderDefault::setNIF;
};

/**
 * @brief A data folder with a few textures and a mesh whose four shapes share one texture set block, mapped by the
 * global PGDirectory.
 */
class PatcherMeshTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    NIFBuilder m_builder;
    std::vector<nifly::BSTriShape*> m_shapes;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    static constexpr const wchar_t* meshPath = L"meshes\\a\\shared.nif";

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }

    [[nodiscard]] static PGTypes::TextureSet original()
    {
        return slots(L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds");
    }

    [[nodiscard]] uint32_t textureSetBlock(nifly::NiShape* shape)
    {
        return m_builder.lightingShader(shape)->textureSetRef.index;
    }

    void SetUp() override
    {
        for (const auto* texture :
             { L"textures\\a\\stone.dds", L"textures\\a\\stone_n.dds", L"textures\\a\\stone_p.dds" }) {
            TestUtil::writeFile(dataDir() / texture, "DDS");
        }

        m_shapes.push_back(m_builder.addShape("First", original()));
        for (const auto* name : { "Second", "Third", "Fourth" }) {
            auto* shape = m_builder.addShape(name, slots(L"textures\\a\\other.dds"));
            // Share the texture set block of the first shape, as many vanilla meshes do.
            m_builder.lightingShader(shape)->textureSetRef
                = nifly::NiBlockRef<nifly::BSShaderTextureSet>(textureSetBlock(m_shapes[0]));
            m_shapes.push_back(shape);
        }
        m_builder.save(dataDir() / meshPath);

        m_pgd = std::make_unique<PGDirectory>(dataDir(), m_dir / L"Output");
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());
    }

    void TearDown() override
    {
        PatcherTextureHookConvertToCM::reset();
        PatcherMeshShaderTransformParallaxToCM::loadOptions(true);
        PGGlobals::setPGD(nullptr);
    }
};

TEST_F(PatcherMeshTest,
       ShouldApplyReadsTheTexturesOfASharedBlockAsTheyAre)
{
    // The shapes share one texture set block, so shouldApply() reads the same textures for all of them. A shape that
    // writes its own result gets a copy of the block, and the others keep reading the original (see PGNIFUtilTest).
    auto& nif = m_builder.nif();
    PatcherMeshShaderDefault patcher(meshPath, &nif);
    std::vector<PatcherMatch> matches;
    for (auto* shape : m_shapes) {
        EXPECT_TRUE(patcher.shouldApply(*shape, matches));
        ASSERT_EQ(matches.size(), 2U);
    }

    auto patched = original();
    patched[0] = L"textures\\a\\missing.dds";
    EXPECT_TRUE(PGNIFUtil::setTextureSlots(&nif, m_shapes[0], patched));
    EXPECT_NE(textureSetBlock(m_shapes[0]), textureSetBlock(m_shapes[1]));
    EXPECT_TRUE(patcher.shouldApply(*m_shapes[0], matches));
    EXPECT_EQ(matches.size(), 1U); // only the normal exists
    EXPECT_TRUE(patcher.shouldApply(*m_shapes[1], matches));
    EXPECT_EQ(matches.size(), 2U); // the other shapes keep the original block
}

TEST(PatcherMeshPlainTest,
     StateOfAPatcher)
{
    NIFBuilder builder;
    auto* shape = builder.addShape("Shape", slots(L"textures\\a\\stone.dds"));

    ExposedPatcher patcher(L"meshes\\a.nif", nullptr);
    EXPECT_EQ(patcher.patcherName(), "Default");
    EXPECT_EQ(patcher.nifPath(), L"meshes\\a.nif");
    EXPECT_THROW(std::ignore = patcher.nif(), std::runtime_error);
    std::vector<PatcherMatch> matches;
    EXPECT_THROW(patcher.shouldApply(*shape, matches), std::runtime_error);

    patcher.setNIF(&builder.nif());
    EXPECT_EQ(patcher.nif(), &builder.nif());

    EXPECT_FALSE(patcher.meshUse().isWeighted);
    EXPECT_EQ(patcher.meshUse().recType, PGPlugin::ModelRecordType::Unknown);
    PGPlugin::MeshUseAttributes use;
    use.isWeighted = true;
    use.isFacegen = true;
    use.recType = PGPlugin::ModelRecordType::ArmorAddon;
    patcher.setMeshUse(use);
    EXPECT_EQ(patcher.meshUse(), use);
}

TEST_F(PatcherMeshTest,
       DefaultPatcherMatchesTheDiffuseAndNormalThatExist)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderDefault patcher(meshPath, &nif);
    EXPECT_EQ(PatcherMeshShaderDefault::shaderType(), PGEnums::ShapeShader::None);
    EXPECT_TRUE(patcher.canApply(*m_shapes[0], true, PGPlugin::ModelRecordType::Grass));

    std::vector<PatcherMatch> matches;
    EXPECT_TRUE(patcher.shouldApply(*m_shapes[0], matches));
    ASSERT_EQ(matches.size(), 2U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone.dds");
    EXPECT_EQ(matches[1].matchedPath, L"textures\\a\\stone_n.dds");
    EXPECT_FALSE(matches[0].extraData);
    EXPECT_EQ(patcher.matchExtraDataHash(matches[0]), 0U);

    // Only textures that exist count, and only the diffuse and normal slots.
    auto other = slots(L"textures\\a\\missing.dds", L"textures\\a\\stone_n.dds");
    other[3] = L"textures\\a\\stone_p.dds";
    EXPECT_TRUE(patcher.shouldApply(other, matches));
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches[0].matchedPath, L"textures\\a\\stone_n.dds");
    EXPECT_FALSE(patcher.shouldApply(slots(L"textures\\a\\missing.dds"), matches));
    EXPECT_TRUE(matches.empty());
    EXPECT_FALSE(patcher.shouldApply(PGTypes::TextureSet { }, matches));

    // Applying it changes nothing.
    const auto before = PGNIFUtil::textureSlots(&nif, m_shapes[0]);
    auto applied = before;
    const auto flags1 = m_builder.lightingShader(m_shapes[0])->shaderFlags1;
    patcher.applyPatch(applied, *m_shapes[0], PatcherMatch { });
    patcher.applyPatchSlots(applied, PatcherMatch { });
    patcher.applyShader(*m_shapes[0]);
    EXPECT_EQ(applied, before);
    EXPECT_EQ(m_builder.lightingShader(m_shapes[0])->shaderFlags1, flags1);

    const auto fromFactory = PatcherMeshShaderDefault::factory()(meshPath, &nif);
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "Default");
}

TEST_F(PatcherMeshTest,
       ParallaxToCMTransformRegistersTheTextureHook)
{
    auto& nif = m_builder.nif();
    PatcherMeshShaderTransformParallaxToCM transform(meshPath, &nif);
    EXPECT_EQ(transform.patcherName(), "UpgradeParallaxToCM");
    EXPECT_EQ(PatcherMeshShaderTransformParallaxToCM::fromShader(), PGEnums::ShapeShader::VanillaParallax);
    EXPECT_EQ(PatcherMeshShaderTransformParallaxToCM::toShader(), PGEnums::ShapeShader::ComplexMaterial);

    PatcherMatch match;
    match.matchedPath = L"textures\\a\\stone_p.dds";
    // By default parallax is only upgraded where it cannot be applied.
    EXPECT_TRUE(transform.shouldTransform(match, false));
    EXPECT_FALSE(transform.shouldTransform(match, true));
    PatcherMeshShaderTransformParallaxToCM::loadOptions(false);
    EXPECT_TRUE(transform.shouldTransform(match, true));
    PatcherMeshShaderTransformParallaxToCM::loadOptions(true);
    EXPECT_FALSE(transform.shouldTransform(match, true));

    PatcherMatch result;
    EXPECT_TRUE(transform.transform(match, result));
    EXPECT_EQ(result.matchedPath, L"textures\\a\\stone_m.dds");
    EXPECT_TRUE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_FALSE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_m.dds"));
    // The texture to be generated counts as a generated file from now on.
    EXPECT_TRUE(m_pgd->isFile(L"textures\\a\\stone_m.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\a\\stone_m.dds"));

    // The result may alias the input, and the factory builds the same patcher.
    const auto fromFactory = PatcherMeshShaderTransformParallaxToCM::factory()(meshPath, &nif);
    ASSERT_TRUE(fromFactory);
    EXPECT_TRUE(fromFactory->transform(match, match));
    EXPECT_EQ(match.matchedPath, L"textures\\a\\stone_m.dds");
}

}
