#pragma once

#include "patchers/base/PatcherMeshGlobal.hpp"

#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshGlobalFinalizeGeometry
 * @brief Global patcher that recomputes bounds, normals, and tangent space for
 * every shape in a NIF, equivalent to running NifSkope's "Update Bounds",
 * "Face Normals", and "Update Tangent Space" mesh operations on the whole file.
 *
 * PGPatcher does not otherwise touch vertex/normal/tangent data, but many
 * source meshes ship with stale or slightly incorrect tangent space data from
 * their export pipeline, which is especially visible as lighting artifacts on
 * normal-mapped complex material/PBR shaders. This patcher is opt-in since
 * recalculating this data is unnecessary (and wasted time) for meshes whose
 * geometry data is already correct.
 */
class PatcherMeshGlobalFinalizeGeometry : public PatcherMeshGlobal {
private:
    static constexpr float defaultSmoothAngle = 60.0F; /** < Default normal smoothing angle threshold, matches nifly's own default */

public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshGlobal::PatcherMeshGlobalFactory
     */
    static PatcherMeshGlobal::PatcherMeshGlobalFactory factory();

    /**
     * @brief Construct a new Patcher Mesh Global Finalize Geometry patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshGlobalFinalizeGeometry(std::filesystem::path nifPath,
                                      nifly::NifFile* nif);

    /**
     * @brief Recompute bounds, normals, and tangent space for every shape in the NIF
     *
     * @return true Geometry data was recomputed for at least one shape
     * @return false NIF had no shapes to process
     */
    bool applyPatch() override;
};
