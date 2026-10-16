#include "patchers/PatcherMeshGlobalRecalculateBounds.hpp"

#include "NIFBuilder.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Object3d.hpp"
#include <gtest/gtest.h>

#include <cmath>

namespace {

using TestUtil::NIFBuilder;

constexpr const wchar_t* meshPath = L"meshes\\a\\stone.nif";

TEST(PatcherMeshGlobalRecalculateBoundsTest,
     FactoryCreatesThePatcher)
{
    NIFBuilder builder;
    const auto patcher = PatcherMeshGlobalRecalculateBounds::factory()(meshPath, &builder.nif());
    ASSERT_NE(patcher, nullptr);
    EXPECT_EQ(patcher->patcherName(), "RecalculateBounds");
}

TEST(PatcherMeshGlobalRecalculateBoundsTest,
     NothingIsPatchedWithoutShapes)
{
    NIFBuilder builder;
    PatcherMeshGlobalRecalculateBounds patcher(meshPath, &builder.nif());
    EXPECT_FALSE(patcher.applyPatch());
}

TEST(PatcherMeshGlobalRecalculateBoundsTest,
     TheBoundingSphereOfEveryShapeIsRecomputed)
{
    NIFBuilder builder;
    auto* first = builder.addShape("First");
    auto* second = builder.addShape("Second");
    // Stale bounds, as a mod that moved the vertices without updating them would leave.
    for (auto* shape : { first, second })
        shape->SetBounds(nifly::BoundingSphere(nifly::Vector3(500.0F, 500.0F, 500.0F), 1.0F));

    PatcherMeshGlobalRecalculateBounds patcher(meshPath, &builder.nif());
    EXPECT_TRUE(patcher.applyPatch());

    // The shapes are unit quads, so the sphere is centered on the quad and goes through its corners.
    for (auto* shape : { first, second }) {
        const auto bounds = shape->GetBounds();
        EXPECT_NEAR(bounds.center.x, 0.5F, 1e-3F) << shape->name.get();
        EXPECT_NEAR(bounds.center.y, 0.5F, 1e-3F) << shape->name.get();
        EXPECT_NEAR(bounds.center.z, 0.0F, 1e-3F) << shape->name.get();
        EXPECT_NEAR(bounds.radius, std::sqrt(0.5F), 1e-3F) << shape->name.get();
    }
}

}
