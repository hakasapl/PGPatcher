#include "patchers/PatcherMeshGlobalRecalculateBounds.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <memory>
#include <utility>

PatcherMeshGlobal::PatcherMeshGlobalFactory PatcherMeshGlobalRecalculateBounds::factory()
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshGlobal> {
        return std::make_unique<PatcherMeshGlobalRecalculateBounds>(nifPath, nif);
    };
}

PatcherMeshGlobalRecalculateBounds::PatcherMeshGlobalRecalculateBounds(std::filesystem::path nifPath,
                                                                       nifly::NifFile* nif)
    : PatcherMeshGlobal(std::move(nifPath),
                        nif,
                        "RecalculateBounds")
{
}

bool PatcherMeshGlobalRecalculateBounds::applyPatch()
{
    const auto shapes = nif()->GetShapes();
    if (shapes.empty())
        return false;

    for (auto* shape : shapes) {
        if (!shape)
            continue;

        // A shape without vertex positions would end up with a zero sphere.
        const auto* verts = nif()->GetVertsForShape(shape);
        if (!verts || verts->empty())
            continue;

        // Recompute the shape's bounding sphere, like NifSkope's "Update Bounds".
        shape->UpdateBounds();
    }

    return true;
}
