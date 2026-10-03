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
        if (shape == nullptr)
            continue;

        // "Update Bounds" - recompute the shape's bounding sphere
        shape->UpdateBounds();
    }

    return true;
}
