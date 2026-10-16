#include "patchers/PatcherTextureGlobalConvertToHDR.hpp"
#include "patchers/PatcherTextureHookConvertToCM.hpp"
#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherTextureHook.hpp"

#include "DDSBuilder.hpp"
#include "PGD3D.hpp"
#include "PGDirectory.hpp"
#include "PGGlobals.hpp"
#include "PGRunObserver.hpp"
#include "TestUtil.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGTypes.hpp"

#include <gtest/gtest.h>

#include <DirectXTex.h>
#include <dxgiformat.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using PGEnums::TextureSlots;
using PGEnums::TextureType;
using TestUtil::DDSBuilder::RGBA;

/**
 * @brief An observer that keeps the generated outputs the hooks report.
 */
class OutputObserver final : public PGRunObserver {
public:
    [[nodiscard]] const std::vector<std::pair<std::filesystem::path,
                                              std::filesystem::path>>&
    outputs() const
    {
        return m_outputs;
    }

    void recordHookOutput([[maybe_unused]] const HookKind& kind,
                          const std::filesystem::path& source,
                          const std::filesystem::path& output) override
    {
        m_outputs.emplace_back(source, output);
    }

private:
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> m_outputs;
};

/**
 * @brief A data folder with a height map and a skin diffuse, mapped by the global PGDirectory, with a global PGD3D that
 * has a GPU when the machine has one.
 */
class PatcherTextureTest : public ::testing::Test {
protected:
    // gtest fixtures share their state with the tests through protected members.
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)
    TestUtil::TempDir m_dir;
    std::unique_ptr<PGDirectory> m_pgd;
    PGD3D m_d3d;
    bool m_hasGPU = false;
    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes,misc-non-private-member-variables-in-classes)

    [[nodiscard]] std::filesystem::path dataDir() const { return m_dir / L"Data"; }
    [[nodiscard]] std::filesystem::path outDir() const { return m_dir / L"Output"; }

    void SetUp() override
    {
        // A height map that is brighter on the right.
        TestUtil::DDSBuilder::writeDDS(dataDir() / L"textures\\a\\stone_p.dds", 32, 32, [](size_t x, size_t) {
            const auto value = static_cast<uint8_t>(x * 8);
            return RGBA { .r = value, .g = value, .b = value, .a = 255 };
        });
        TestUtil::DDSBuilder::writeDDS(
            dataDir() / L"textures\\actors\\skin.dds", 64, 32, RGBA { .r = 200, .g = 150, .b = 120, .a = 255 });
        TestUtil::DDSBuilder::writeDDS(
            dataDir() / L"textures\\a\\plain.dds", 8, 8, RGBA { .r = 64, .g = 128, .b = 192, .a = 255 });

        m_pgd = std::make_unique<PGDirectory>(dataDir(), outDir());
        m_pgd->populateFileMap(false, false);
        PGGlobals::setPGD(m_pgd.get());
        PGGlobals::setPGD3D(&m_d3d);
        m_hasGPU = m_d3d.initGPU() && m_d3d.initShaders();
    }

    void TearDown() override
    {
        PGRunObserver::install(nullptr);
        PatcherTextureHookConvertToCM::reset();
        PatcherTextureHookFixSSS::reset();
        PGGlobals::setPGD3D(nullptr);
        PGGlobals::setPGD(nullptr);
    }

    void requireGPU() const
    {
        if (!m_hasGPU)
            GTEST_SKIP() << "no Direct3D 11 hardware device";
    }

    [[nodiscard]] static DirectX::TexMetadata metadataOf(const std::filesystem::path& file)
    {
        DirectX::TexMetadata meta { };
        EXPECT_HRESULT_SUCCEEDED(DirectX::GetMetadataFromDDSFile(file.c_str(), DirectX::DDS_FLAGS_NONE, meta));
        return meta;
    }
};

TEST_F(PatcherTextureTest,
       ConvertToCMHookTracksTheTexturesToConvert)
{
    EXPECT_EQ(PatcherTextureHookConvertToCM::outputFilename(L"textures\\a\\stone_p.dds"), L"textures\\a\\stone_m.dds");
    EXPECT_EQ(PatcherTextureHookConvertToCM::outputFilename(L"textures\\a\\stone.dds"), L"textures\\a\\stone_m.dds");
    EXPECT_FALSE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(PatcherTextureHookConvertToCM::processList().empty());

    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds");
    PatcherTextureHookConvertToCM::addToProcessList(L"textures\\a\\stone_p.dds"); // again
    EXPECT_TRUE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_FALSE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_m.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\a\\stone_m.dds"));
    const std::vector<std::filesystem::path> registered { L"textures\\a\\stone_p.dds" };
    EXPECT_EQ(PatcherTextureHookConvertToCM::processList(), registered);

    PatcherTextureHookConvertToCM::reset();
    EXPECT_FALSE(PatcherTextureHookConvertToCM::isInProcessList(L"textures\\a\\stone_p.dds"));
    EXPECT_TRUE(PatcherTextureHookConvertToCM::processList().empty());

    // Replaying a generated texture of a previous run records its type without converting anything. The generated map
    // is not added to the texture maps, which only hold mapped textures.
    PatcherTextureHookConvertToCM::replayGenerated(L"textures\\a\\stone_p.dds");
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(L"textures\\a\\stone"));
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_m.dds"), TextureType::ComplexMaterial);

    // A patcher without a texture cannot run.
    PatcherTextureHookConvertToCM patcher(L"textures\\a\\stone_p.dds", nullptr);
    EXPECT_EQ(patcher.patcherName(), "ParallaxToCM");
    EXPECT_THROW(patcher.applyPatch(), std::runtime_error);
    DirectX::ScratchImage none;
    EXPECT_THROW(patcher.generate(none), std::runtime_error);
}

TEST_F(PatcherTextureTest,
       FixSSSHookTracksTheTexturesToConvert)
{
    EXPECT_EQ(PatcherTextureHookFixSSS::outputFilename(L"textures\\actors\\skin.dds"), L"textures\\actors\\skin_s.dds");
    EXPECT_FALSE(PatcherTextureHookFixSSS::isInProcessList(L"textures\\actors\\skin.dds"));

    PatcherTextureHookFixSSS::addToProcessList(L"textures\\actors\\skin.dds");
    EXPECT_TRUE(PatcherTextureHookFixSSS::isInProcessList(L"textures\\actors\\skin.dds"));
    EXPECT_TRUE(m_pgd->isGenerated(L"textures\\actors\\skin_s.dds"));
    const std::vector<std::filesystem::path> registered { L"textures\\actors\\skin.dds" };
    EXPECT_EQ(PatcherTextureHookFixSSS::processList(), registered);
    PatcherTextureHookFixSSS::reset();
    EXPECT_FALSE(PatcherTextureHookFixSSS::isInProcessList(L"textures\\actors\\skin.dds"));
    EXPECT_TRUE(PatcherTextureHookFixSSS::processList().empty());

    PatcherTextureHookFixSSS::replayGenerated(L"textures\\actors\\skin.dds");
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::Glow).contains(L"textures\\actors\\skin"));
    EXPECT_EQ(m_pgd->textureType(L"textures\\actors\\skin_s.dds"), TextureType::SubsurfaceColor);

    PatcherTextureHookFixSSS patcher(L"textures\\actors\\skin.dds", nullptr);
    EXPECT_EQ(patcher.patcherName(), "SSSFix");
    EXPECT_THROW(patcher.applyPatch(), std::runtime_error);
    DirectX::ScratchImage none;
    EXPECT_THROW(patcher.generate(none), std::runtime_error);
}

TEST_F(PatcherTextureTest,
       HookSignaturesTellTheGenerationsApart)
{
    // The signature covers the shader and the parameters of a generation; a cache of generated textures keys on it.
    EXPECT_EQ(PatcherTextureHookConvertToCM::generatorSignature(), PatcherTextureHookConvertToCM::generatorSignature());
    EXPECT_EQ(PatcherTextureHookFixSSS::generatorSignature(), PatcherTextureHookFixSSS::generatorSignature());
    EXPECT_NE(PatcherTextureHookConvertToCM::generatorSignature(), PatcherTextureHookFixSSS::generatorSignature());
}

TEST_F(PatcherTextureTest,
       ConvertToCMWritesAComplexMaterialMapOnTheGPU)
{
    requireGPU();
    ASSERT_TRUE(PatcherTextureHookConvertToCM::initShader());
    EXPECT_TRUE(PatcherTextureHookConvertToCM::initShader()); // compiled once

    DirectX::ScratchImage height;
    ASSERT_TRUE(m_d3d.getDDS(L"textures\\a\\stone_p.dds", height));
    PatcherTextureHookConvertToCM patcher(L"textures\\a\\stone_p.dds", &height);

    // The in-memory generation is what the output file receives; it writes and records nothing itself.
    const auto output = outDir() / L"textures\\a\\stone_m.dds";
    DirectX::ScratchImage generated;
    ASSERT_TRUE(patcher.generate(generated));
    EXPECT_EQ(generated.GetMetadata().width, 32U);
    EXPECT_EQ(generated.GetMetadata().height, 32U);
    EXPECT_EQ(generated.GetMetadata().format, DXGI_FORMAT_BC3_UNORM);
    EXPECT_EQ(generated.GetMetadata().mipLevels, 6U);
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_m.dds"), TextureType::Unknown);

    OutputObserver observer;
    PGRunObserver::install(&observer);
    EXPECT_TRUE(patcher.applyPatch());
    PGRunObserver::install(nullptr);
    ASSERT_EQ(observer.outputs().size(), 1U);
    EXPECT_EQ(observer.outputs()[0].first, L"textures\\a\\stone_p.dds");
    EXPECT_EQ(observer.outputs()[0].second, L"textures\\a\\stone_m.dds");

    ASSERT_TRUE(std::filesystem::exists(output));
    const auto meta = metadataOf(output);
    EXPECT_EQ(meta.width, 32U);
    EXPECT_EQ(meta.height, 32U);
    EXPECT_EQ(meta.format, DXGI_FORMAT_BC3_UNORM);

    // The height ends up in the alpha channel of the complex material map.
    DirectX::ScratchImage compressed;
    ASSERT_HRESULT_SUCCEEDED(DirectX::LoadFromDDSFile(output.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, compressed));
    DirectX::ScratchImage pixels;
    ASSERT_HRESULT_SUCCEEDED(DirectX::Decompress(*compressed.GetImage(0, 0, 0), DXGI_FORMAT_R8G8B8A8_UNORM, pixels));
    const std::span<const uint8_t> row(pixels.GetImage(0, 0, 0)->pixels, pixels.GetImage(0, 0, 0)->slicePitch);
    EXPECT_LT(row[3], 40); // left: dark
    EXPECT_GT(row[(31 * 4) + 3], 200); // right: bright
    EXPECT_EQ(row[0], 0);

    // The type is recorded; the generated map is not a texture map entry, it is reached through the hook.
    EXPECT_FALSE(m_pgd->textureMapConst(TextureSlots::EnvMask).contains(L"textures\\a\\stone"));
    EXPECT_EQ(m_pgd->textureType(L"textures\\a\\stone_m.dds"), TextureType::ComplexMaterial);
}

TEST_F(PatcherTextureTest,
       FixSSSWritesAHalfSizeSubsurfaceMapOnTheGPU)
{
    requireGPU();
    ASSERT_TRUE(PatcherTextureHookFixSSS::initShader());

    DirectX::ScratchImage skin;
    ASSERT_TRUE(m_d3d.getDDS(L"textures\\actors\\skin.dds", skin));
    PatcherTextureHookFixSSS patcher(L"textures\\actors\\skin.dds", &skin);

    // The in-memory generation is the half size map the output file receives.
    const auto output = outDir() / L"textures\\actors\\skin_s.dds";
    DirectX::ScratchImage generated;
    ASSERT_TRUE(patcher.generate(generated));
    EXPECT_EQ(generated.GetMetadata().width, 32U);
    EXPECT_EQ(generated.GetMetadata().height, 16U);
    EXPECT_EQ(generated.GetMetadata().format, DXGI_FORMAT_BC2_UNORM);
    EXPECT_FALSE(std::filesystem::exists(output));

    OutputObserver observer;
    PGRunObserver::install(&observer);
    EXPECT_TRUE(patcher.applyPatch());
    PGRunObserver::install(nullptr);
    ASSERT_EQ(observer.outputs().size(), 1U);
    EXPECT_EQ(observer.outputs()[0].second, L"textures\\actors\\skin_s.dds");

    ASSERT_TRUE(std::filesystem::exists(output));
    const auto meta = metadataOf(output);
    EXPECT_EQ(meta.width, 32U);
    EXPECT_EQ(meta.height, 16U);
    EXPECT_EQ(meta.format, DXGI_FORMAT_BC2_UNORM);
    EXPECT_EQ(m_pgd->textureType(L"textures\\actors\\skin_s.dds"), TextureType::SubsurfaceColor);
}

TEST_F(PatcherTextureTest,
       ConvertToHDRScalesTheLuminanceIntoAFloatTexture)
{
    const std::unordered_map<std::string, std::string> options {
        { "luminance_mult", "2" },
        { "output_format", "rgba32f" },
        { "something_else", "ignored" },
    };
    PatcherTextureGlobalConvertToHDR::loadOptions(options);

    DirectX::ScratchImage plain;
    ASSERT_TRUE(m_d3d.getDDS(L"textures\\a\\plain.dds", plain));
    const auto fromFactory = PatcherTextureGlobalConvertToHDR::factory()(L"textures\\a\\plain.dds", &plain);
    ASSERT_TRUE(fromFactory);
    EXPECT_EQ(fromFactory->patcherName(), "ConvertToHDR");

    if (!m_hasGPU) {
        // Without a GPU the texture is left as it is.
        bool isModified = false;
        fromFactory->applyPatch(isModified);
        EXPECT_FALSE(isModified);
        EXPECT_EQ(plain.GetMetadata().format, DXGI_FORMAT_R8G8B8A8_UNORM);
        GTEST_SKIP() << "no Direct3D 11 hardware device";
    }

    ASSERT_TRUE(PatcherTextureGlobalConvertToHDR::initShader());
    bool isModified = false;
    fromFactory->applyPatch(isModified);
    EXPECT_TRUE(isModified);
    ASSERT_EQ(plain.GetMetadata().format, DXGI_FORMAT_R32G32B32A32_FLOAT);
    EXPECT_EQ(plain.GetMetadata().width, 8U);

    // (64, 128, 192) / 255 times two, read from the raw float texels.
    std::array<float, 4> pixel { };
    std::memcpy(pixel.data(), plain.GetImage(0, 0, 0)->pixels, sizeof(pixel));
    EXPECT_NEAR(pixel[0], 2 * 64.0F / 255.0F, 0.01F);
    EXPECT_NEAR(pixel[1], 2 * 128.0F / 255.0F, 0.01F);
    EXPECT_NEAR(pixel[2], 2 * 192.0F / 255.0F, 0.01F);
    EXPECT_NEAR(pixel[3], 1, 0.01F);

    // Back to the defaults for the other tests.
    const std::unordered_map<std::string, std::string> defaults {
        { "luminance_mult", "1" },
        { "output_format", "rgba16f" },
    };
    PatcherTextureGlobalConvertToHDR::loadOptions(defaults);
}

TEST_F(PatcherTextureTest,
       TheHookDispatchByKindMatchesTheHooks)
{
    using HookKind = PGRunObserver::HookKind;
    const std::filesystem::path height = L"textures\\a\\stone_p.dds";
    const std::filesystem::path diffuse = L"textures\\a\\skin.dds";
    EXPECT_EQ(PatcherTextureHook::outputPathOf(HookKind::ConvertToCM, height),
              PatcherTextureHookConvertToCM::outputFilename(height));
    EXPECT_EQ(PatcherTextureHook::outputPathOf(HookKind::FixSSS, diffuse),
              PatcherTextureHookFixSSS::outputFilename(diffuse));
    EXPECT_EQ(PatcherTextureHook::outputTypeOf(HookKind::ConvertToCM), PGEnums::TextureType::ComplexMaterial);
    EXPECT_EQ(PatcherTextureHook::outputTypeOf(HookKind::FixSSS), PGEnums::TextureType::SubsurfaceColor);
    EXPECT_EQ(PatcherTextureHook::generatorSignatureOf(HookKind::ConvertToCM),
              PatcherTextureHookConvertToCM::generatorSignature());
    EXPECT_EQ(PatcherTextureHook::generatorSignatureOf(HookKind::FixSSS),
              PatcherTextureHookFixSSS::generatorSignature());
    EXPECT_STREQ(PatcherTextureHook::hookNameOf(HookKind::ConvertToCM), "ParallaxToCM");
    EXPECT_STREQ(PatcherTextureHook::hookNameOf(HookKind::FixSSS), "SSSFix");
}

/// @brief Reaches the protected signature function of the hook base.
struct SignatureProbe : PatcherTextureHook {
    using PatcherTextureHook::signatureOf;
};

TEST_F(PatcherTextureTest,
       TheSignatureChangesWithTheBytecodeAndWithEveryValue)
{
    const std::array<unsigned char, 4> bytecode { 1, 2, 3, 4 };
    const std::array<unsigned char, 4> otherBytecode { 1, 2, 3, 5 };
    const std::array<std::uint32_t, 2> values { 10, 20 };
    const std::array<std::uint32_t, 2> otherValues { 10, 21 };
    const std::array<std::uint32_t, 3> moreValues { 10, 20, 0 };
    const auto signature = SignatureProbe::signatureOf(bytecode, values);
    EXPECT_EQ(signature, SignatureProbe::signatureOf(bytecode, values));
    EXPECT_NE(signature, SignatureProbe::signatureOf(otherBytecode, values));
    EXPECT_NE(signature, SignatureProbe::signatureOf(bytecode, otherValues));
    EXPECT_NE(signature, SignatureProbe::signatureOf(bytecode, moreValues));
}

}
