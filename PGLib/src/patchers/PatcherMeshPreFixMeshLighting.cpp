#include "patchers/PatcherMeshPreFixMeshLighting.hpp"

#include "patchers/base/PatcherMeshPre.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPreFixMeshLighting::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPre> {
        return std::make_unique<PatcherMeshPreFixMeshLighting>(nifPath, nif);
    };
}

PatcherMeshPreFixMeshLighting::PatcherMeshPreFixMeshLighting(std::filesystem::path nifPath,
                                                             nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath),
                     nif,
                     "FixMeshLighting")
{
}

auto PatcherMeshPreFixMeshLighting::applyPatch([[maybe_unused]] PGTypes::TextureSet& slots,
                                               nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // Not a BSLightingShaderProperty.
        return false;
    }

    const auto shaderType = nifShaderBSLSP->GetShaderType();
    if (shaderType != nifly::BSLSP_DEFAULT) {
        // Only patch the default shader type.
        return false;
    }

    if (nifShaderBSLSP->softlighting <= softLightingMax)
        return false;

    PGNIFUtil::setShaderFloat(nifShaderBSLSP->softlighting, softLightingMax);

    return true;
}
