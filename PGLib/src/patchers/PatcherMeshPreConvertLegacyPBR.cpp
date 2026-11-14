#include "patchers/PatcherMeshPreConvertLegacyPBR.hpp"

#include "patchers/PatcherMeshShaderTruePBR.hpp"
#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPreConvertLegacyPBR::factory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPre> {
        return std::make_unique<PatcherMeshPreConvertLegacyPBR>(nifPath, nif);
    };
}

PatcherMeshPreConvertLegacyPBR::PatcherMeshPreConvertLegacyPBR(std::filesystem::path nifPath,
                                                               nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath),
                     nif,
                     "ConvertLegacyPBR")
{
}

bool PatcherMeshPreConvertLegacyPBR::applyPatch(PGTypes::TextureSet& slots,
                                                nifly::NiShape& nifShape)
{
    if (PatcherMeshShaderTruePBR::format() != PGEnums::TruePBRFormat::Standalone)
        return false;

    // This also leaves shapes with PG_IGNORE alone.
    if (!PGNIFUtil::isShaderPatchableShape(*nif(), nifShape))
        return false;

    return PatcherMeshShaderTruePBR::convertLegacyShape(*nif(), nifShape, slots);
}
