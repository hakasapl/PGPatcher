#include "patchers/PatcherMeshPostFixSSS.hpp"

#include "patchers/PatcherTextureHookFixSSS.hpp"
#include "patchers/base/PatcherMeshPost.hpp"
#include "util/NIFUtil.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <boost/algorithm/string/predicate.hpp>

#include <filesystem>
#include <memory>
#include <utility>

using namespace std;

auto PatcherMeshPostFixSSS::getFactory() -> PatcherMeshPost::PatcherMeshPostFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPost> {
        return make_unique<PatcherMeshPostFixSSS>(nifPath, nif);
    };
}

PatcherMeshPostFixSSS::PatcherMeshPostFixSSS(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPost(std::move(nifPath), nif, "FixSSS")
{
}

auto PatcherMeshPostFixSSS::applyPatch(NIFUtil::TextureSet& slots, nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);
    if (nifShaderBSLSP == nullptr) {
        // not a BSLightingShaderProperty
        return false;
    }

    if (!NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)) {
        // we don't care if it doesn't have soft lighting
        return false;
    }

    // check if diffuse and glow are the same
    const auto& diffuseMap = slots.at(static_cast<int>(NIFUtil::TextureSlots::DIFFUSE));
    auto& glowMap = slots.at(static_cast<int>(NIFUtil::TextureSlots::GLOW));

    if (!boost::iequals(diffuseMap, glowMap)) {
        return false;
    }

    if (diffuseMap.empty()) {
        return false;
    }

    // verify that diffuseMap is a DDS file
    if (!boost::iends_with(diffuseMap, ".dds")) {
        return false;
    }

    // create texture hook
    PatcherTextureHookFixSSS::addToProcessList(diffuseMap);

    glowMap = PatcherTextureHookFixSSS::getOutputFilename(diffuseMap);

    return true;
}
