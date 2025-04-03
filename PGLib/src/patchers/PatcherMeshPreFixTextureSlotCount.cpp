#include "patchers/PatcherMeshPreFixTextureSlotCount.hpp"

using namespace std;

auto PatcherMeshPreFixTextureSlotCount::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPre> {
        return make_unique<PatcherMeshPreFixTextureSlotCount>(nifPath, nif);
    };
}

PatcherMeshPreFixTextureSlotCount::PatcherMeshPreFixTextureSlotCount(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath), nif, "FixTextureSlotCount", false)
{
}

auto PatcherMeshPreFixTextureSlotCount::applyPatch(nifly::NiShape& nifShape) -> bool
{
    bool changed = false;

    auto* nifShader = getNIF()->GetShader(&nifShape);

    auto* txstRec = getNIF()->GetHeader().GetBlock(nifShader->TextureSetRef());
    if (txstRec == nullptr) {
        return false;
    }

    if (txstRec->textures.size() < SLOT_COUNT) {
        txstRec->textures.resize(SLOT_COUNT);
        changed = true;
    }

    return changed;
}
