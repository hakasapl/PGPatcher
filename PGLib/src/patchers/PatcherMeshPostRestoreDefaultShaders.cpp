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

using namespace std;

auto PatcherMeshPostRestoreDefaultShaders::getFactory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPost> {
        return make_unique<PatcherMeshPostRestoreDefaultShaders>(nifPath, nif);
    };
}

PatcherMeshPostRestoreDefaultShaders::PatcherMeshPostRestoreDefaultShaders(std::filesystem::path nifPath,
                                                                           nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath),
                      nif,
                      "HairFlowMap")
{
}

auto PatcherMeshPostRestoreDefaultShaders::applyPatch(PGTypes::TextureSet& slots,
                                                      nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        return false;
    }

    if (restoreDefaultShaderFromParallax(slots, *nifShaderBSLSP)) {
        return true;
    }

    if (restoreDefaultShaderFromComplexMaterial(slots, *nifShaderBSLSP)) {
        return true;
    }

    return false;
}

auto PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromParallax(PGTypes::TextureSet& slots,
                                                                            nifly::BSLightingShaderProperty& shaderProp)
    -> bool
{
    auto* pgd = PGGlobals::getPGD();

    if (shaderProp.GetShaderType() != BSLSP_PARALLAX) {
        return false;
    }

    // this is parallax type, check the _p texture to see if it exists
    const auto& parallaxTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::PARALLAX)));

    if (pgd->isFile(parallaxTex)) {
        // definitely a parallax map, no need to disable
        return false;
    }

    // not a parallax map, restore to default shader
    shaderProp.SetShaderType(BSLSP_DEFAULT);
    PGNIFUtil::clearShaderFlag(&shaderProp, SLSF1_PARALLAX);

    // clear parallax texture slot
    slots.at(static_cast<int>(PGEnums::TextureSlots::PARALLAX)).clear();

    return true;
}

auto PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromComplexMaterial(
    PGTypes::TextureSet& slots,
    nifly::BSLightingShaderProperty& shaderProp) -> bool
{
    auto* pgd = PGGlobals::getPGD();

    if (shaderProp.GetShaderType() != BSLSP_ENVMAP) {
        return false;
    }

    // this is complex material type, check the _cm texture to see if it exists
    const auto& envTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::CUBEMAP)));
    const auto& envMaskTex = StringUtil::toLowerASCIIFast(slots.at(static_cast<int>(PGEnums::TextureSlots::ENVMASK)));

    const bool envValid = envTex.empty() || pgd->isFile(envTex)
        || StringUtil::asciiFastIEquals(envTex, PatcherMeshShaderComplexMaterial::s_DYNCUBEMAPPATH);
    const bool envMaskValid = envMaskTex.empty() || pgd->isFile(envMaskTex);
    if (envValid && envMaskValid) {
        // cubemap and env mask valid, no need to disable
        return false;
    }

    // not a complex material map, restore to default shader
    shaderProp.SetShaderType(BSLSP_DEFAULT);
    PGNIFUtil::clearShaderFlag(&shaderProp, SLSF1_ENVIRONMENT_MAPPING);

    // clear complex material texture slot
    slots.at(static_cast<int>(PGEnums::TextureSlots::CUBEMAP)).clear();
    slots.at(static_cast<int>(PGEnums::TextureSlots::ENVMASK)).clear();

    return true;
}
