#pragma once

#include "patchers/base/PatcherMeshGlobal.hpp"

#include "Animation.hpp"
#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Nodes.hpp"
#include "Shaders.hpp"
#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <mutex>
#include <string>

/**
 * @class PatcherMeshGlobalParticleLightsToLP
 * @brief Global patcher that replaces particle lights (additive soft effect billboards) with Light Placer lights
 *
 * The matching billboard nodes are removed from the mesh and an equivalent light entry (position, color, radius, fade
 * and fade controller) is collected for finalize(), which writes them all to LightPlacer\parallaxgen.json.
 */
class PatcherMeshGlobalParticleLightsToLP : public PatcherMeshGlobal {
private:
    static nlohmann::json s_lpJsonData; /** < LP JSON data */
    static std::mutex s_lpJsonDataMutex; /** < Mutex for LP JSON data */

    static constexpr int particleLightFlags = 4109; /** < Particle light flags */
    static constexpr int whiteColor = 255; /** < White color */
    static constexpr double minValue = 1e-5; /** < Minimum value */
    static constexpr float roundingValue = 1000000; /** < Rounding value */

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshGlobal::PatcherMeshGlobalFactory
     */
    static PatcherMeshGlobal::PatcherMeshGlobalFactory factory();

    /**
     * @brief Construct a new Particle Lights To LP patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshGlobalParticleLightsToLP(std::filesystem::path nifPath,
                                        nifly::NifFile* nif);

    /**
     * @brief Convert every particle light of the NIF and delete its billboard node (and blocks only it referenced)
     *
     * @return true At least one particle light was converted
     * @return false Nothing was changed
     */
    bool applyPatch() override;

    /**
     * @brief Write the collected lights, merged by model and light data, to LightPlacer\parallaxgen.json in the output
     * folder. Call once after all meshes were patched; nothing is written when no light was collected
     */
    static void finalize();

private:
    /**
     * @brief Collect the Light Placer entry of one particle light
     *
     * @param node billboard node, which gives the light's position
     * @param shape BSTriShape of the billboard, which gives the radius (mean vertex distance) and, for a white base
     * color, the color from its vertex colors
     * @param effectShader effect shader, which gives the base color, fade and controllers
     * @return true if an entry was collected
     * @return false if the shape is not a BSTriShape
     */
    bool applySinglePatch(nifly::NiBillboardNode* node,
                          nifly::NiShape* shape,
                          nifly::BSEffectShaderProperty* effectShader);

    /**
     * @brief Get LP JSON for a specific NIF controller
     *
     * Only the emissive multiple controller of the effect shader is exported, as the fade controller of the light.
     *
     * @param controller Controller to get JSON for
     * @param jsonField JSON field to store controller JSON in LP
     * @return nlohmann::json JSON for controller, empty if the controller is not exported
     */
    nlohmann::json getControllerJSON(nifly::NiTimeController* controller,
                                     std::string& jsonField);
};
