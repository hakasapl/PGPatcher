#include "patchers/PatcherMeshPreFixEffectLightingCS.hpp"
#include "NIFUtil.hpp"

using namespace std;

auto PatcherMeshPreFixEffectLightingCS::getFactory() -> PatcherMeshPre::PatcherMeshPreFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshPre> {
        return make_unique<PatcherMeshPreFixEffectLightingCS>(nifPath, nif);
    };
}

PatcherMeshPreFixEffectLightingCS::PatcherMeshPreFixEffectLightingCS(std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshPre(std::move(nifPath), nif, "FixMeshLighting")
{
}

auto PatcherMeshPreFixEffectLightingCS::applyPatch(nifly::NiShape& nifShape) -> bool
{
    // Get the shader
    auto* nifShader = getNIF()->GetShader(&nifShape);
    auto* effectShader = dynamic_cast<nifly::BSEffectShaderProperty*>(nifShader);

    if (effectShader == nullptr) {
        // Not a BSEffectShaderProperty, skip
        return false;
    }

    bool changed = false;

    // Check if the shader has the effect lighting flag set
    if (NIFUtil::hasShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags2::SLSF2_EFFECT_LIGHTING)) {
        // Set uniform scale flag
        changed |= NIFUtil::setShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags2::SLSF2_UNIFORM_SCALE);

        // clear external emmitance flag
        changed |= NIFUtil::clearShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags1::SLSF1_EXTERNAL_EMITTANCE);

        // Set lighting influence, which is the second byte (uint8_t) in the uint32_t
        uint32_t& clampMode = effectShader->textureClampMode;
        static constexpr uint8_t LIGHTING_INFLUENCE_OFFSET = 8;

        static constexpr uint8_t CLEAR_MASK = 0xFF;
        clampMode &= ~(CLEAR_MASK << LIGHTING_INFLUENCE_OFFSET);

        static constexpr uint8_t LIGHTING_INFLUENCE_SETVAL = 255;
        clampMode |= (LIGHTING_INFLUENCE_SETVAL << LIGHTING_INFLUENCE_OFFSET);
    }

    return changed;
}
