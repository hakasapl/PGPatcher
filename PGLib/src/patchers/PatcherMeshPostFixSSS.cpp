#include "patchers/PatcherMeshPostFixSSS.hpp"

#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string/predicate.hpp>

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPostFixSSS::factory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPost> {
        return std::make_unique<PatcherMeshPostFixSSS>(nifPath, nif);
    };
}

PatcherMeshPostFixSSS::PatcherMeshPostFixSSS(std::filesystem::path nifPath,
                                             nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath),
                      nif,
                      "FixSSS")
{
}

bool PatcherMeshPostFixSSS::applyPatch(PGTypes::TextureSet& slots,
                                       nifly::NiShape& nifShape)
{
    auto* nifShader = nif()->GetShader(&nifShape);
    const auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
    if (!nifShaderBSLSP) {
        // Not a BSLightingShaderProperty.
        return false;
    }

    const auto shaderType = nifShaderBSLSP->GetShaderType();
    if (shaderType != nifly::BSLSP_DEFAULT) {
        // Only patch the default shader type.
        return false;
    }

    if (!PGNIFUtil::hasShaderFlag(nifShaderBSLSP, nifly::SLSF2_SOFT_LIGHTING)) {
        // We don't care if it doesn't have soft lighting.
        return false;
    }

    // Check if diffuse and glow are the same.
    const auto& diffuseMap = slots.at(static_cast<int>(PGEnums::TextureSlots::Diffuse));
    auto& glowMap = slots.at(static_cast<int>(PGEnums::TextureSlots::Glow));

    if (!boost::iequals(diffuseMap, glowMap))
        return false;

    if (diffuseMap.empty())
        return false;

    // Verify that diffuseMap is a DDS file.
    if (!boost::iends_with(diffuseMap, ".dds"))
        return false;

    // Create texture hook.
    PatcherTextureHookFixSSS::addToProcessList(diffuseMap);

    glowMap = PatcherTextureHookFixSSS::outputFilename(diffuseMap);

    return true;
}
