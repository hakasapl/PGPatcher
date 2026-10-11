#include "PGD3D.hpp"

#include "BSABuilder.hpp"
#include "DDSBuilder.hpp"
#include "FakeGame.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "TestUtil.hpp"
#include "common/BethesdaGame.hpp"

#include <gtest/gtest.h>

#include <DirectXTex.h>
#include <d3d11.h>
#include <spdlog/common.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TestLog = TestUtil::TestLog;
using TestUtil::DDSBuilder::RGBA;

/**
 * @brief A game whose data folder holds a few textures, loose and archived, mapped by a PGDirectory that is the global
 * one while the fixture lives.
 */
class PGD3DTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::FakeGame m_fake;
    std::optional<BethesdaGame> m_game;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d { TestUtil::shaderDir() };
    bool m_hasGPU = false;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    void SetUp() override
    {
        TestUtil::DDSBuilder::writeDDS(
            m_fake.dataDir() / L"textures\\square.dds", 64, 64, RGBA { .r = 255, .g = 0, .b = 0, .a = 255 });
        TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\square2.dds", 32, 32);
        TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\wide.dds", 128, 64);
        TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\wide2.dds", 256, 128);
        TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\tall.dds", 64, 128);
        TestUtil::writeFile(m_fake.dataDir() / L"textures\\bad.dds", "this is not a dds file");

        m_fake.addPlugin(L"Tex.esp");
        TestUtil::BSABuilder archive;
        archive.addFile("textures\\archived.dds",
                        TestUtil::DDSBuilder::ddsBytes(TestUtil::DDSBuilder::rgbaImage(16, 8)));
        archive.addFile("textures\\archived_bad.dds", "not a dds");
        archive.write(m_fake.dataDir() / L"Tex.bsa");

        m_game.emplace(m_fake.game());
        m_pgd = std::make_unique<PGDirectory>(&*m_game, m_fake.gameDir() / L"out");
        m_pgd->populateFileMap(true, false);
        PGGlobals::setPGD(m_pgd.get());

        m_hasGPU = m_d3d.initGPU() && m_d3d.initShaders();
    }

    void TearDown() override { PGGlobals::setPGD(nullptr); }

    void requireGPU() const
    {
        if (!m_hasGPU)
            GTEST_SKIP() << "no Direct3D 11 hardware device";
    }
};

TEST(PGD3DStaticTest,
     DxgiFormatFromString)
{
    EXPECT_EQ(PGD3D::dxgiFormatFromString("rgba16f"), DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(PGD3D::dxgiFormatFromString("rgba32f"), DXGI_FORMAT_R32G32B32A32_FLOAT);
    EXPECT_EQ(PGD3D::dxgiFormatFromString("RGBA16F"), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(PGD3D::dxgiFormatFromString("rgba8"), DXGI_FORMAT_UNKNOWN);
    EXPECT_EQ(PGD3D::dxgiFormatFromString(""), DXGI_FORMAT_UNKNOWN);
}

TEST(PGD3DStaticTest,
     HresultErrorMessageDescribesTheError)
{
    EXPECT_FALSE(PGD3D::hresultErrorMessage(E_FAIL).empty());
    EXPECT_FALSE(PGD3D::hresultErrorMessage(E_INVALIDARG).empty());
    EXPECT_NE(PGD3D::hresultErrorMessage(E_FAIL), PGD3D::hresultErrorMessage(E_INVALIDARG));
}

TEST(PGD3DStaticTest,
     GpuFunctionsThrowBeforeInitGpu)
{
    PGD3D d3d(TestUtil::shaderDir());
    const auto image = TestUtil::DDSBuilder::rgbaImage(16, 16);

    std::array<int, 4> counts { };
    EXPECT_THROW(d3d.countPixelValues(image, counts), std::runtime_error);

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    EXPECT_THROW(d3d.initShader("CountAlphaValues.hlsl", shader), std::runtime_error);
    EXPECT_THROW(d3d.initShaders(), std::runtime_error);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    EXPECT_THROW(d3d.createTexture2D(image, texture), std::runtime_error);
    Microsoft::WRL::ComPtr<ID3D11Texture2D> other;
    EXPECT_THROW(d3d.createTexture2D(texture, other), std::runtime_error);
    D3D11_TEXTURE2D_DESC desc = { };
    EXPECT_THROW(d3d.createTexture2D(desc, texture), std::runtime_error);

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    EXPECT_THROW(d3d.createShaderResourceView(texture, srv), std::runtime_error);
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    EXPECT_THROW(d3d.createUnorderedAccessView(texture, uav), std::runtime_error);
    const D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = { };
    EXPECT_THROW(d3d.createUnorderedAccessView(Microsoft::WRL::ComPtr<ID3D11Resource>(), uavDesc, uav),
                 std::runtime_error);

    D3D11_BUFFER_DESC bufferDesc = { };
    Microsoft::WRL::ComPtr<ID3D11Buffer> buffer;
    EXPECT_THROW(d3d.createBuffer(nullptr, bufferDesc, buffer), std::runtime_error);
    EXPECT_THROW(d3d.createConstantBuffer(nullptr, 16, buffer), std::runtime_error);
    EXPECT_THROW(d3d.blockingDispatch(shader, { }, { }, { }, 1, 1, 1), std::runtime_error);

    DirectX::ScratchImage out;
    EXPECT_THROW(d3d.readBack(texture, out), std::runtime_error);
    EXPECT_THROW(d3d.copyResource(nullptr, nullptr), std::runtime_error);
    EXPECT_THROW(d3d.generateMips(srv), std::runtime_error);
    EXPECT_THROW(d3d.flushGPU(), std::runtime_error);

    // A missing shader is rejected before anything touches the GPU.
    EXPECT_THROW(d3d.applyShaderToTexture(image, out, shader), std::runtime_error);
}

TEST_F(PGD3DTest,
       GetDdsReadsLooseAndArchivedTextures)
{
    DirectX::ScratchImage image;
    ASSERT_TRUE(m_d3d.getDDS(L"textures\\square.dds", image));
    EXPECT_EQ(image.GetMetadata().width, 64U);
    EXPECT_EQ(image.GetMetadata().height, 64U);
    EXPECT_EQ(image.GetMetadata().format, DXGI_FORMAT_R8G8B8A8_UNORM);
    const std::span<const uint8_t> pixels(image.GetImage(0, 0, 0)->pixels, image.GetImage(0, 0, 0)->slicePitch);
    EXPECT_EQ(pixels[0], 255);
    EXPECT_EQ(pixels[1], 0);

    DirectX::ScratchImage archived;
    ASSERT_TRUE(m_d3d.getDDS(L"textures\\archived.dds", archived));
    EXPECT_EQ(archived.GetMetadata().width, 16U);
    EXPECT_EQ(archived.GetMetadata().height, 8U);

    DirectX::ScratchImage none;
    EXPECT_FALSE(m_d3d.getDDS(L"textures\\missing.dds", none));
    EXPECT_FALSE(m_d3d.getDDS(L"textures\\bad.dds", none));
    EXPECT_FALSE(m_d3d.getDDS(L"textures\\archived_bad.dds", none));

    // Without the global directory nothing can be read.
    PGGlobals::setPGD(nullptr);
    EXPECT_THROW(m_d3d.getDDS(L"textures\\square.dds", none), std::runtime_error);
}

TEST_F(PGD3DTest,
       GetDdsMetadataCachesEveryTextureItRead)
{
    DirectX::TexMetadata meta { };
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\wide.dds", meta));
    EXPECT_EQ(meta.width, 128U);
    EXPECT_EQ(meta.height, 64U);
    EXPECT_EQ(meta.format, DXGI_FORMAT_R8G8B8A8_UNORM);

    DirectX::TexMetadata archivedMeta { };
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\archived.dds", archivedMeta));
    EXPECT_EQ(archivedMeta.width, 16U);

    // The file is not read again once it is cached.
    std::filesystem::remove(m_fake.dataDir() / L"textures\\wide.dds");
    DirectX::TexMetadata cached { };
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\wide.dds", cached));
    EXPECT_EQ(cached.width, 128U);

    DirectX::TexMetadata none { };
    EXPECT_FALSE(m_d3d.getDDSMetadata(L"textures\\missing.dds", none));
    EXPECT_FALSE(m_d3d.getDDSMetadata(L"textures\\bad.dds", none));

    const auto snapshot = m_d3d.ddsMetadataCacheSnapshot();
    EXPECT_EQ(snapshot.size(), 2U);
    EXPECT_TRUE(snapshot.contains(L"textures\\wide.dds"));
    EXPECT_TRUE(snapshot.contains(L"textures\\archived.dds"));
    EXPECT_FALSE(snapshot.contains(L"textures\\bad.dds"));
}

TEST_F(PGD3DTest,
       SeededMetadataIsUsedWithoutReadingAndNeverOverwrites)
{
    DirectX::TexMetadata seeded { };
    seeded.width = 1024;
    seeded.height = 512;
    seeded.format = DXGI_FORMAT_BC3_UNORM;
    m_d3d.seedDDSMetadata(L"textures\\seeded.dds", seeded); // no such file

    DirectX::TexMetadata meta { };
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\seeded.dds", meta));
    EXPECT_EQ(meta.width, 1024U);
    EXPECT_EQ(meta.format, DXGI_FORMAT_BC3_UNORM);

    // An entry that exists is kept.
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\square.dds", meta));
    m_d3d.seedDDSMetadata(L"textures\\square.dds", seeded);
    ASSERT_TRUE(m_d3d.getDDSMetadata(L"textures\\square.dds", meta));
    EXPECT_EQ(meta.width, 64U);
}

TEST_F(PGD3DTest,
       CheckIfAspectRatioMatchesComparesHeaders)
{
    EXPECT_TRUE(m_d3d.checkIfAspectRatioMatches(L"textures\\square.dds", L"textures\\square2.dds"));
    EXPECT_TRUE(m_d3d.checkIfAspectRatioMatches(L"textures\\wide.dds", L"textures\\wide2.dds"));
    EXPECT_TRUE(m_d3d.checkIfAspectRatioMatches(L"textures\\archived.dds", L"textures\\wide.dds"));
    EXPECT_FALSE(m_d3d.checkIfAspectRatioMatches(L"textures\\wide.dds", L"textures\\tall.dds"));
    EXPECT_FALSE(m_d3d.checkIfAspectRatioMatches(L"textures\\square.dds", L"textures\\wide.dds"));

    EXPECT_FALSE(m_d3d.checkIfAspectRatioMatches(L"textures\\square.dds", L"textures\\missing.dds"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Unable to process texture: textures\\missing.dds"));
    EXPECT_FALSE(m_d3d.checkIfAspectRatioMatches(L"textures\\bad.dds", L"textures\\square.dds"));
    EXPECT_TRUE(TestLog::contains(spdlog::level::err, "Unable to process texture: textures\\bad.dds"));
}

TEST_F(PGD3DTest,
       CountPixelValuesCountsEveryChannel)
{
    requireGPU();

    // Red in the left half, green in the top half, blue too faint to count, opaque in the left quarter.
    const auto image = TestUtil::DDSBuilder::rgbaImage(16, 16, [](size_t x, size_t y) {
        return RGBA {
            .r = static_cast<uint8_t>(x < 8 ? 200 : 0),
            .g = static_cast<uint8_t>(y < 8 ? 10 : 0),
            .b = 3,
            .a = static_cast<uint8_t>(x < 4 ? 255 : 0),
        };
    });

    std::array<int, 4> counts { };
    ASSERT_TRUE(m_d3d.countPixelValues(image, counts));
    EXPECT_EQ(counts[0], 128);
    EXPECT_EQ(counts[1], 128);
    EXPECT_EQ(counts[2], 0);
    EXPECT_EQ(counts[3], 64);

    // Textures that are not powers of two cannot be uploaded.
    const auto odd = TestUtil::DDSBuilder::rgbaImage(48, 48);
    EXPECT_FALSE(m_d3d.countPixelValues(odd, counts));
}

TEST_F(PGD3DTest,
       CheckIfCmClassifiesEnvironmentMasks)
{
    requireGPU();

    // Opaque alpha everywhere: a plain environment mask.
    TestUtil::DDSBuilder::writeDDS(
        m_fake.dataDir() / L"textures\\opaque_m.dds", 16, 16, RGBA { .r = 200, .g = 200, .b = 200, .a = 255 });
    // No alpha channel at all.
    TestUtil::DDSBuilder::writeDDS(
        m_fake.dataDir() / L"textures\\bc1_m.dds",
        TestUtil::DDSBuilder::compress(TestUtil::DDSBuilder::rgbaImage(16, 16), DXGI_FORMAT_BC1_UNORM));
    // Transparent, with only the red and blue channels in use.
    TestUtil::DDSBuilder::writeDDS(
        m_fake.dataDir() / L"textures\\cm_rb_m.dds", 16, 16, RGBA { .r = 255, .g = 0, .b = 100, .a = 0 });
    // Transparent with every channel empty.
    TestUtil::DDSBuilder::writeDDS(
        m_fake.dataDir() / L"textures\\cm_empty_m.dds", 16, 16, RGBA { .r = 0, .g = 0, .b = 0, .a = 0 });
    // Exactly half the pixels opaque is still a complex material texture, more than half is not.
    TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\cm_half_m.dds", 16, 16, [](size_t x, size_t) {
        return RGBA { .r = 0, .g = 50, .b = 0, .a = static_cast<uint8_t>(x < 8 ? 255 : 0) };
    });
    TestUtil::DDSBuilder::writeDDS(m_fake.dataDir() / L"textures\\cm_most_m.dds", 16, 16, [](size_t x, size_t) {
        return RGBA { .r = 0, .g = 50, .b = 0, .a = static_cast<uint8_t>(x < 9 ? 255 : 0) };
    });
    m_pgd->populateFileMap(true, false);

    const auto classify = [this](const std::wstring& path, bool& isCM, bool& envMask, bool& gloss, bool& metal) {
        isCM = false;
        envMask = false;
        gloss = false;
        metal = false;
        return m_d3d.checkIfCM(path, isCM, envMask, gloss, metal);
    };

    bool isCM = false;
    bool envMask = false;
    bool gloss = false;
    bool metal = false;

    EXPECT_TRUE(classify(L"textures\\opaque_m.dds", isCM, envMask, gloss, metal));
    EXPECT_FALSE(isCM);

    EXPECT_TRUE(classify(L"textures\\bc1_m.dds", isCM, envMask, gloss, metal));
    EXPECT_FALSE(isCM);

    EXPECT_TRUE(classify(L"textures\\cm_rb_m.dds", isCM, envMask, gloss, metal));
    EXPECT_TRUE(isCM);
    EXPECT_TRUE(envMask);
    EXPECT_FALSE(gloss);
    EXPECT_TRUE(metal);

    EXPECT_TRUE(classify(L"textures\\cm_empty_m.dds", isCM, envMask, gloss, metal));
    EXPECT_TRUE(isCM);
    EXPECT_FALSE(envMask);
    EXPECT_FALSE(gloss);
    EXPECT_FALSE(metal);

    EXPECT_TRUE(classify(L"textures\\cm_half_m.dds", isCM, envMask, gloss, metal));
    EXPECT_TRUE(isCM);
    EXPECT_TRUE(gloss);

    EXPECT_TRUE(classify(L"textures\\cm_most_m.dds", isCM, envMask, gloss, metal));
    EXPECT_FALSE(isCM);

    EXPECT_FALSE(classify(L"textures\\missing.dds", isCM, envMask, gloss, metal));
    EXPECT_FALSE(isCM);
    EXPECT_FALSE(classify(L"textures\\bad.dds", isCM, envMask, gloss, metal));
    EXPECT_FALSE(isCM);
}

TEST_F(PGD3DTest,
       ApplyShaderToTextureRunsAComputeShaderAndBuildsMips)
{
    requireGPU();

    const TestUtil::TempDir shaders;
    TestUtil::writeFile(shaders / L"Half.hlsl",
                        "Texture2D<float4> Input : register(t0);\n"
                        "RWTexture2D<float4> Output : register(u0);\n"
                        "[numthreads(16, 16, 1)]\n"
                        "void main(uint3 id : SV_DispatchThreadID)\n"
                        "{\n"
                        "    Output[id.xy] = Input.Load(uint3(id.xy, 0)) * 0.5;\n"
                        "}\n");
    TestUtil::writeFile(shaders / L"Broken.hlsl", "this does not compile");

    PGD3D d3d(shaders.path());
    ASSERT_TRUE(d3d.initGPU());

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> half;
    ASSERT_TRUE(d3d.initShader("Half.hlsl", half));
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> broken;
    EXPECT_FALSE(d3d.initShader("Broken.hlsl", broken));
    Microsoft::WRL::ComPtr<ID3D11ComputeShader> missing;
    EXPECT_FALSE(d3d.initShader("Missing.hlsl", missing));

    const auto input = TestUtil::DDSBuilder::rgbaImage(32, 32, RGBA { .r = 200, .g = 100, .b = 50, .a = 255 });
    DirectX::ScratchImage output;
    ASSERT_TRUE(d3d.applyShaderToTexture(input, output, half));
    const auto meta = output.GetMetadata();
    EXPECT_EQ(meta.width, 32U);
    EXPECT_EQ(meta.height, 32U);
    EXPECT_EQ(meta.format, DXGI_FORMAT_R8G8B8A8_UNORM);
    EXPECT_EQ(meta.mipLevels, 6U); // 32, 16, 8, 4, 2, 1

    const std::span<const uint8_t> pixels(output.GetImage(0, 0, 0)->pixels, output.GetImage(0, 0, 0)->slicePitch);
    EXPECT_NEAR(pixels[0], 100, 1);
    EXPECT_NEAR(pixels[1], 50, 1);
    EXPECT_NEAR(pixels[2], 25, 1);
    EXPECT_NEAR(pixels[3], 128, 1);

    // The last mip is the average of a uniform image, so it has the same color.
    const std::span<const uint8_t> lastMip(output.GetImage(meta.mipLevels - 1, 0, 0)->pixels,
                                           output.GetImage(meta.mipLevels - 1, 0, 0)->slicePitch);
    EXPECT_NEAR(lastMip[0], 100, 1);

    // The output can have another size and format.
    DirectX::ScratchImage smaller;
    ASSERT_TRUE(d3d.applyShaderToTexture(input, smaller, half, DXGI_FORMAT_R16G16B16A16_FLOAT, 16, 16));
    EXPECT_EQ(smaller.GetMetadata().width, 16U);
    EXPECT_EQ(smaller.GetMetadata().height, 16U);
    EXPECT_EQ(smaller.GetMetadata().format, DXGI_FORMAT_R16G16B16A16_FLOAT);
    EXPECT_EQ(smaller.GetMetadata().mipLevels, 5U);

    // Inputs that are not powers of two fail, as does an empty image.
    DirectX::ScratchImage none;
    EXPECT_FALSE(d3d.applyShaderToTexture(TestUtil::DDSBuilder::rgbaImage(48, 32), none, half));
    EXPECT_FALSE(d3d.applyShaderToTexture(DirectX::ScratchImage(), none, half));

    // Constant buffers of any size can be created.
    const std::array<float, 3> params { 1, 2, 3 };
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants;
    EXPECT_TRUE(d3d.createConstantBuffer(params.data(), sizeof(params), constants));
    DirectX::ScratchImage withParams;
    EXPECT_TRUE(d3d.applyShaderToTexture(
        input, withParams, half, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 0, params.data(), sizeof(params)));
}

}
