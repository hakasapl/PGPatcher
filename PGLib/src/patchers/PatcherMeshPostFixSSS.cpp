#include "patchers/PatcherMeshPostFixSSS.hpp"
#include "NIFUtil.hpp"

#include "patchers/PatcherTextureHookFixSSS.hpp"
#include <boost/algorithm/string/predicate.hpp>

#include "Logger.hpp"

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

auto PatcherMeshPostFixSSS::applyPatch(nifly::NiShape& nifShape) -> bool
{
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* const nifShaderBSLSP = dynamic_cast<BSLightingShaderProperty*>(nifShader);

    if (!NIFUtil::hasShaderFlag(nifShaderBSLSP, SLSF2_SOFT_LIGHTING)) {
        // we don't care if it doesn't have soft lighting
        return false;
    }

    // check if diffuse and glow are the same
    const auto diffuseMap = NIFUtil::getTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::DIFFUSE);
    const auto glowMap = NIFUtil::getTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::GLOW);

    if (!boost::iequals(diffuseMap, glowMap) && !glowMap.empty()) {
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
    filesystem::path newSSSMap;

    {
        PatcherTextureHookFixSSS texHook(diffuseMap);
        if (!texHook.applyPatch(newSSSMap)) {
            Logger::error("Failed to fix SSS for texture: {}", diffuseMap);
            return false;
        }
    }

    return NIFUtil::setTextureSlot(getNIF(), &nifShape, NIFUtil::TextureSlots::GLOW, newSSSMap);
}
