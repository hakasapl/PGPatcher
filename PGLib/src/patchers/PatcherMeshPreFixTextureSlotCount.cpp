#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPreFixTextureSlotCount::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPre> {
        return std::make_unique<PatcherMeshPreFixTextureSlotCount>(nifPath, nif);
    };
}

PatcherMeshPreFixTextureSlotCount::PatcherMeshPreFixTextureSlotCount(std::filesystem::path nifPath,
                                                                     nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath),
                     nif,
                     "FixTextureSlotCount")
{
}

auto PatcherMeshPreFixTextureSlotCount::applyPatch([[maybe_unused]] PGTypes::TextureSet& slots,
                                                   nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);

    auto* txstRec = getNIF()->GetHeader().GetBlock(nifShader->TextureSetRef());
    if (txstRec == nullptr)
        return false;

    if (txstRec->textures.size() >= slotCount)
        return false;

    txstRec->textures.resize(slotCount);
    return true;
}
