#pragma warning(push)
#pragma warning(disable : 4834)

#include "BethesdaGame.hpp"
#include "CommonTests.hpp"
#include "ParallaxGenD3D.hpp"
#include "ParallaxGenDirectory.hpp"

#include <DirectXTex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace std;

// NOLINTBEGIN(misc-non-private-member-variables-in-classes,cppcoreguidelines-non-private-member-variables-in-classes)
class ParallaxGenD3DTest : public ::testing::TestWithParam<PGTesting::TestEnvGameParams> {
protected:
    // Set up code for each test
    void SetUp() override
    {
        const auto& params = GetParam();

        m_bg = make_unique<BethesdaGame>(params.GameType, false, params.GamePath, params.AppDataPath,
            params.DocumentPath); // no logging
        m_pgd = make_unique<ParallaxGenDirectory>(m_bg.get(), "", nullptr); // no logging

        m_pgd3d = make_unique<ParallaxGenD3D>(m_pgd.get(), PGTestEnvs::s_exePath / "shaders");
    }

    // Tear down code for each test
    void TearDown() override
    {
        // Clean up after each test
    }

    unique_ptr<BethesdaGame> m_bg;
    unique_ptr<ParallaxGenDirectory> m_pgd;
    unique_ptr<ParallaxGenD3D> m_pgd3d;
};
// NOLINTEND(misc-non-private-member-variables-in-classes,cppcoreguidelines-non-private-member-variables-in-classes)

TEST_P(ParallaxGenD3DTest, TextureTests)
{
    const vector<wstring> bsaExcludes { L"Skyrim - Textures5.bsa" };
    m_pgd->populateFileMap(false); // includeBSAs = false, only loose files
    m_pgd->mapFiles({}, {}, {}, bsaExcludes, true); // MapFromMeshes = false, map only based on texture name for quick
                                                    // test runs and less dependency to PGD in this test

    EXPECT_THROW(m_pgd3d->extendedTexClassify(bsaExcludes), runtime_error);
    // EXPECT_THROW(m_pgd3d->upgradeToComplexMaterial("", ""), runtime_error);

    m_pgd3d->initGPU();

    // make sure before conversion the type is ENVMASK and after the conversion COMPLEXMATERIAL
    auto envMaskMap = m_pgd->getTextureMapConst(NIFUtil::TextureSlots::ENVMASK);

    auto mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impdirt01");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impdirt01_m.dds", NIFUtil::TextureType::ENVIRONMENTMASK })
        != mapEntry->second.end());

    mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impextwall01");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impextwall01_m.dds", NIFUtil::TextureType::ENVIRONMENTMASK })
        != mapEntry->second.end());

    mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impwall06");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impwall06_m.dds", NIFUtil::TextureType::ENVIRONMENTMASK })
        != mapEntry->second.end());

    const bool anyComplexMaterial = std::ranges::any_of(envMaskMap, [](auto mapEntry) {
        return any_of(mapEntry.second.begin(), mapEntry.second.end(),
            [](const auto& texture) { return (texture.type == NIFUtil::TextureType::COMPLEXMATERIAL); });
    });

    EXPECT_FALSE(anyComplexMaterial);

    // start algorithm
    // EXPECT_TRUE(m_pgd3d->findCMMaps(bsaExcludes) == ParallaxGenTask::PGResult::SUCCESS);

    envMaskMap = m_pgd->getTextureMapConst(NIFUtil::TextureSlots::ENVMASK);

    auto numComplexMaterial = count_if(envMaskMap.begin(), envMaskMap.end(), [](auto mapEntry) {
        return any_of(mapEntry.second.begin(), mapEntry.second.end(),
            [](const auto& texture) { return (texture.type == NIFUtil::TextureType::COMPLEXMATERIAL); });
    });

    EXPECT_TRUE(numComplexMaterial == 15);

    mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impdirt01");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impdirt01_m.dds", NIFUtil::TextureType::COMPLEXMATERIAL })
        != mapEntry->second.end());

    mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impextwall01");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impextwall01_m.dds", NIFUtil::TextureType::COMPLEXMATERIAL })
        != mapEntry->second.end());

    mapEntry = envMaskMap.find(L"textures\\dungeons\\imperial\\impwall06");
    EXPECT_FALSE(mapEntry == envMaskMap.end());
    EXPECT_TRUE(mapEntry->second.find(
                    { "textures\\dungeons\\imperial\\impwall06_m.dds", NIFUtil::TextureType::COMPLEXMATERIAL })
        != mapEntry->second.end());

    // aspect ratio
    EXPECT_FALSE(m_pgd3d->checkIfAspectRatioMatches(
        L"textures\\clutter\\common\\rug01.dds", L"textures\\dungeons\\imperial\\impdirt01_m.dds"));

    EXPECT_TRUE(m_pgd3d->checkIfAspectRatioMatches(
        L"textures\\clutter\\common\\rug01.dds", L"textures\\clutter\\common\\rug01_p.dds"));

    EXPECT_TRUE(m_pgd3d->checkIfAspectRatioMatches(
        L"textures\\dungeons\\imperial\\impextwall01_m.dds", L"textures\\dungeons\\imperial\\impdirt01_m.dds"));

    // upgrade to complex material
    // auto image = m_pgd3d->upgradeToComplexMaterial("", "");
    // EXPECT_TRUE(image.GetImageCount() == 0);

    const filesystem::path rug01Path { L"textures\\clutter\\common\\rug01_p.dds" };

    // complex material map should not produce an output
    // image = m_pgd3d->upgradeToComplexMaterial("", L"textures\\dungeons\\imperial\\impdirt01_m.dds");
    // EXPECT_TRUE(image.GetImageCount() == 0);

    // load and decompress
    DirectX::ScratchImage rugImage;
    DirectX::LoadFromDDSFile((m_bg->getGameDataPath() / rug01Path).c_str(), DirectX::DDS_FLAGS_NONE, nullptr, rugImage);

    DirectX::ScratchImage rugImagedecompressed;
    DirectX::Decompress(rugImage.GetImages(), rugImage.GetImageCount(), rugImage.GetMetadata(),
        DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, rugImagedecompressed);

    // image = m_pgd3d->upgradeToComplexMaterial(rug01Path, "");

    // constexpr unsigned RUG01_WIDTH = 256;
    // constexpr unsigned RUG01_HEIGHT = 512;
    // constexpr unsigned RUG01_MIPLEVELS = 10;
    // EXPECT_TRUE(image.GetMetadata().format == DXGI_FORMAT::DXGI_FORMAT_BC3_UNORM);
    // EXPECT_TRUE(image.GetMetadata().mipLevels == RUG01_MIPLEVELS);
    // EXPECT_TRUE(image.GetMetadata().width == RUG01_WIDTH);
    // EXPECT_TRUE(image.GetMetadata().height == RUG01_HEIGHT);

    // check that rgb are all 0 and alpha is now the r value from the height map
    // DirectX::ScratchImage decompressed;
    // DirectX::Decompress(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
    //     DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM, decompressed);
    // uint8_t* pixels = decompressed.GetPixels();
    // uint8_t* rugPixels = rugImagedecompressed.GetPixels();
    // constexpr unsigned ALLWOWEDDIFFERENCE = 4; // due to compression/decompression the values are not exactly equal
    // for (int i = 0; i < decompressed.GetPixelsSize(); i += 4) {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    //    ASSERT_TRUE(pixels[i] == 0); // r
    //    ASSERT_TRUE(pixels[i + 1] == 0); // g
    //    ASSERT_TRUE(pixels[i + 2] == 0); // b
    //    ASSERT_TRUE(abs(pixels[i + 3] - rugPixels[i]) <= ALLWOWEDDIFFERENCE); // alpha
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    //}
}

INSTANTIATE_TEST_SUITE_P(GameParametersSE, ParallaxGenD3DTest, ::testing::Values(PGTestEnvs::s_testENVSkyrimSE));

#pragma warning(pop)
