#include "TestUtil.hpp"
#include "VanillaAssets.hpp"
#include "common/BethesdaDirectory.hpp"
#include "common/BethesdaGame.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "util/ContainerUtil.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace VanillaAssets = TestUtil::VanillaAssets;

TEST(VanillaMeshesTest,
     VanillaArchivesAreMappedAndTheirMeshesLoad)
{
    if (!VanillaAssets::isAvailable())
        GTEST_SKIP() << "no vanilla Skyrim Special Edition install";

    BethesdaGame game(BethesdaGame::GameType::SkyrimSE, VanillaAssets::gameDir(), VanillaAssets::appDataDir());
    const TestUtil::TempDir out;
    BethesdaDirectory bd(&game, { L"meshes" }, out.path());

    const auto loadOrder = bd.bsaLoadOrder();
    EXPECT_TRUE(ContainerUtil::isInVector(loadOrder, std::wstring(L"Skyrim - Meshes0.bsa")));
    EXPECT_TRUE(ContainerUtil::isInVector(loadOrder, std::wstring(L"Skyrim - Meshes1.bsa")));

    bd.populateFileMap(true, true);

    std::vector<std::filesystem::path> meshes;
    std::filesystem::path ironArmorMesh;
    for (const auto& [path, file] : bd.fileMap()) {
        if (path.extension() != L".nif")
            continue;

        meshes.push_back(path);
        if (ironArmorMesh.empty() && path.wstring().starts_with(L"meshes\\armor\\iron\\"))
            ironArmorMesh = path;
    }
    EXPECT_GT(meshes.size(), 10000U);

    // The vanilla meshes come from the archives, never loose.
    ASSERT_FALSE(ironArmorMesh.empty());
    EXPECT_TRUE(bd.isBSAFile(ironArmorMesh)) << TestUtil::pathToUTF8(ironArmorMesh);
    EXPECT_TRUE(bd.isFileInBSA(ironArmorMesh, loadOrder)) << TestUtil::pathToUTF8(ironArmorMesh);
    EXPECT_EQ(bd.fileIdentity(ironArmorMesh).kind, BethesdaDirectory::FileIdentity::Kind::BSA);

    // Every mesh in a full run, a sample otherwise, so the default test run stays quick.
    const size_t step = VanillaAssets::isFullRunRequested() ? 1 : 25;
    size_t loaded = 0;
    for (size_t i = 0; i < meshes.size(); i += step) {
        const auto& mesh = meshes[i];
        const auto bytes = bd.file(mesh);
        ASSERT_FALSE(bytes.empty()) << TestUtil::pathToUTF8(mesh);
        EXPECT_NO_THROW(PGNIFUtil::loadNIFFromBytes(bytes)) << TestUtil::pathToUTF8(mesh);
        loaded++;
    }
    EXPECT_GT(loaded, 100U);
}

}
