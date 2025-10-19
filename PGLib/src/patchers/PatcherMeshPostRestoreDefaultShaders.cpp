#include "patchers/PatcherMeshPostRestoreDefaultShaders.hpp"

#include "patchers/base/PatcherMeshPost.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include "util/ParallaxGenUtil.hpp"
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

PatcherMeshPostRestoreDefaultShaders::PatcherMeshPostRestoreDefaultShaders(
    std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath), nif, "HairFlowMap")
{
}

auto PatcherMeshPostRestoreDefaultShaders::applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool
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

auto PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromParallax(
    NIFUtil::TextureSet& slots, nifly::BSLightingShaderProperty& shaderProp) -> bool
{
    if (shaderProp.GetShaderType() != BSLSP_PARALLAX) {
        return false;
    }

    // this is parallax type, check the _p texture to see if it exists
    const auto& parallaxTex
        = ParallaxGenUtil::toLowerASCIIFast(slots.at(static_cast<int>(NIFUtil::TextureSlots::PARALLAX)));
    if (getPGD()->isFile(parallaxTex)) {
        // definitely a parallax map, no need to disable
        return false;
    }

    // not a parallax map, restore to default shader
    shaderProp.SetShaderType(BSLSP_DEFAULT);
    NIFUtil::clearShaderFlag(&shaderProp, SLSF1_PARALLAX);

    // clear parallax texture slot
    slots.at(static_cast<int>(NIFUtil::TextureSlots::PARALLAX)).clear();

    return true;
}

auto PatcherMeshPostRestoreDefaultShaders::restoreDefaultShaderFromComplexMaterial(
    NIFUtil::TextureSet& slots, nifly::BSLightingShaderProperty& shaderProp) -> bool
{
    if (shaderProp.GetShaderType() != BSLSP_ENVMAP) {
        return false;
    }

    // this is complex material type, check the _cm texture to see if it exists
    const auto& envTex = ParallaxGenUtil::toLowerASCIIFast(slots.at(static_cast<int>(NIFUtil::TextureSlots::CUBEMAP)));
    const auto& envMaskTex
        = ParallaxGenUtil::toLowerASCIIFast(slots.at(static_cast<int>(NIFUtil::TextureSlots::ENVMASK)));
    if (getPGD()->isFile(envTex) && (envMaskTex.empty() || getPGD()->isFile(envMaskTex))) {
        // cubemap and env mask exists, no need to disable
        return false;
    }

    // not a complex material map, restore to default shader
    shaderProp.SetShaderType(BSLSP_DEFAULT);
    NIFUtil::clearShaderFlag(&shaderProp, SLSF1_ENVIRONMENT_MAPPING);

    // clear complex material texture slot
    slots.at(static_cast<int>(NIFUtil::TextureSlots::CUBEMAP)).clear();
    slots.at(static_cast<int>(NIFUtil::TextureSlots::ENVMASK)).clear();

    return true;
}
