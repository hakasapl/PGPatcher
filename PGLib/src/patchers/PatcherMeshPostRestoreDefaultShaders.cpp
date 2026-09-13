#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"

#include "PGGlobals.hpp"
#include "patchers/PatcherMeshShaderComplexMaterial.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "pgutil/PGEnums.hpp"
#include "pgutil/PGNIFUtil.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include "util/StringUtil.hpp"
#include <boost/algorithm/string/case_conv.hpp>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <utility>

auto PatcherMeshPostRestoreDefaultShaders::factory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const std::filesystem::path& nifPath, nifly::NifFile* nif) -> std::unique_ptr<PatcherMeshPost> {
        return std::make_unique<PatcherMeshPostRestoreDefaultShaders>(nifPath, nif);
    };
}

PatcherMeshPostRestoreDefaultShaders::PatcherMeshPostRestoreDefaultShaders(std::filesystem::path nifPath,
                                                                           nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath),
                      nif,
                      "HairFlowMap")
{
}

bool PatcherMeshPostRestoreDefaultShaders::applyPatch(PGTypes::TextureSet& slots,
                                                      nifly::NiShape& nifShape)
{
    auto* nifShader = nif()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<nifly::BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr)
        return false;

    if (restoreDefaultShaderFromParallax(slots, *nifShaderBSLSP))
        return true;

    if (restoreDefaultShaderFromComplexMaterial(slots, *nifShaderBSLSP))
        return true;

    return false;
}

bool PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromParallax(PGTypes::TextureSet& slots,
                                                                            nifly::BSLightingShaderProperty& shaderProp)
{
    auto* pgd = PGGlobals::pgd();

    if (shaderProp.GetShaderType() != nifly::BSLSP_PARALLAX)
        return false;

    // This is parallax type, check the _p texture to see if it exists.
    const auto& parallaxTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::Parallax)));

    if (pgd->isFile(parallaxTex)) {
        // Definitely a parallax map, no need to disable.
        return false;
    }

    // Not a parallax map, restore to default shader.
    shaderProp.SetShaderType(nifly::BSLSP_DEFAULT);
    PGNIFUtil::clearShaderFlag(&shaderProp, nifly::SLSF1_PARALLAX);

    // Clear parallax texture slot.
    slots.at(static_cast<int>(PGEnums::TextureSlots::Parallax)).clear();

    return true;
}

bool PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromComplexMaterial(
    PGTypes::TextureSet& slots,
    nifly::BSLightingShaderProperty& shaderProp)
{
    auto* pgd = PGGlobals::pgd();

    if (shaderProp.GetShaderType() != nifly::BSLSP_ENVMAP)
        return false;

    // This is complex material type, check the _cm texture to see if it exists.
    const auto& envTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::Cubemap)));
    const auto& envMaskTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::EnvMask)));

    const bool envValid = envTex.empty() || pgd->isFile(envTex)
        || StringUtil::asciiFastIEquals(envTex, PatcherMeshShaderComplexMaterial::s_dynCubemapPath);
    const bool envMaskValid = envMaskTex.empty() || pgd->isFile(envMaskTex);
    if (envValid && envMaskValid) {
        // Cubemap and env mask valid, no need to disable.
        return false;
    }

    // Not a complex material map, restore to default shader.
    shaderProp.SetShaderType(nifly::BSLSP_DEFAULT);
    PGNIFUtil::clearShaderFlag(&shaderProp, nifly::SLSF1_ENVIRONMENT_MAPPING);

    // Clear complex material texture slot.
    slots.at(static_cast<int>(PGEnums::TextureSlots::Cubemap)).clear();
    slots.at(static_cast<int>(PGEnums::TextureSlots::EnvMask)).clear();

    return true;
}
