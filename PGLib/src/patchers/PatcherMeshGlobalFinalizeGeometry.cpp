#include "patchers/PatcherMeshGlobalFinalizeGeometry.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <memory>
#include <utility>

PatcherMeshGlobal::PatcherMeshGlobalFactory PatcherMeshGlobalFinalizeGeometry::factory()
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshGlobal> {
        return std::make_unique<PatcherMeshGlobalFinalizeGeometry>(nifPath, nif);
    };
}

PatcherMeshGlobalFinalizeGeometry::PatcherMeshGlobalFinalizeGeometry(std::filesystem::path nifPath,
                                                                     nifly::NifFile* nif)
    : PatcherMeshGlobal(std::move(nifPath),
                        nif,
                        "FinalizeGeometry")
{
}

bool PatcherMeshGlobalFinalizeGeometry::applyPatch()
{
    const auto shapes = nif()->GetShapes();
    if (shapes.empty())
        return false;

    for (auto* shape : shapes) {
        if (shape == nullptr)
            continue;

        // "Face Normals" / "Smooth Normals" - recompute per-vertex normals
        nif()->CalcNormalsForShape(shape, false, true, defaultSmoothAngle);

        // "Update Tangent Space" - requires normals and UVs to already be set,
        // which is guaranteed since CalcNormalsForShape() just ran above
        nif()->CalcTangentsForShape(shape);

        // "Update Bounds" - recompute the shape's bounding sphere
        shape->UpdateBounds();
    }

    return true;
}
