#include "patchers/PatcherMeshGlobalFixEffectLightingCS.hpp"
#include "util/NIFUtil.hpp"

using namespace std;

PatcherMeshGlobalFixEffectLightingCS::PatcherMeshGlobalFixEffectLightingCS(
    std::filesystem::path nifPath, nifly::NifFile* nif)
    : PatcherMeshGlobal(std::move(nifPath), nif, "FixEffectLightingCS")
{
}

auto PatcherMeshGlobalFixEffectLightingCS::getFactory() -> PatcherMeshGlobal::PatcherMeshGlobalFactory
{
    return [](const filesystem::path& nifPath, nifly::NifFile* nif) -> unique_ptr<PatcherMeshGlobal> {
        return make_unique<PatcherMeshGlobalFixEffectLightingCS>(nifPath, nif);
    };
}

auto PatcherMeshGlobalFixEffectLightingCS::applyPatch() -> bool
{
    // loop through every effect shader in the NIF
    vector<NiObject*> nifBlockTree;
    getNIF()->GetTree(nifBlockTree);

    bool changed = false;

    for (NiObject* nifBlock : nifBlockTree) {
        auto* const effectShader = dynamic_cast<nifly::BSEffectShaderProperty*>(nifBlock);
        if (effectShader == nullptr) {
            // Not a BSEffectShaderProperty, skip
            continue;
        }

        if (NIFUtil::hasShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags2::SLSF2_UNIFORM_SCALE)) {
            // Already has the uniform scale flag set, skip (assumed to be already patched)
            continue;
        }

        // Check if the shader has the effect lighting flag set
        if (NIFUtil::hasShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags2::SLSF2_EFFECT_LIGHTING)) {
            // Set uniform scale flag
            changed |= NIFUtil::setShaderFlag(effectShader, nifly::SkyrimShaderPropertyFlags2::SLSF2_UNIFORM_SCALE);

            // clear external emmitance flag
            // changed
            //    |= NIFUtil::clearShaderFlag(effectShader,
            //    nifly::SkyrimShaderPropertyFlags1::SLSF1_EXTERNAL_EMITTANCE);

            // Set lighting influence, which is the second byte (uint8_t) in the uint32_t
            uint32_t& clampMode = effectShader->textureClampMode;
            static constexpr uint8_t LIGHTING_INFLUENCE_OFFSET = 8;

            // get current value
            static constexpr uint8_t CLEAR_MASK = 0xFF;
            const uint8_t currentLightingInfluence = (clampMode >> LIGHTING_INFLUENCE_OFFSET) & CLEAR_MASK;

            static constexpr uint8_t LIGHTING_INFLUENCE_SETVAL = 255;
            if (currentLightingInfluence != LIGHTING_INFLUENCE_SETVAL) {
                changed = true;

                clampMode &= ~(CLEAR_MASK << LIGHTING_INFLUENCE_OFFSET);
                clampMode |= (LIGHTING_INFLUENCE_SETVAL << LIGHTING_INFLUENCE_OFFSET);
            }
        }
    }

    return changed;
}
