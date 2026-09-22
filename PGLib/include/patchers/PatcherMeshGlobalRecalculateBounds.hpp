#pragma once

#include "patchers/base/PatcherMeshGlobal.hpp"

#include "NifFile.hpp"

#include <filesystem>

/**
 * @class PatcherMeshGlobalRecalculateBounds
 * @brief Global patcher that recomputes the bounding sphere for every shape in
 * a NIF, equivalent to running NifSkope's "Update Bounds" mesh operation on
 * the whole file.
 *
 * PGPatcher does not otherwise touch shape bounds, but a patched mesh's
 * geometry can differ from the source mesh's, which is what the bounding
 * sphere stored in the NIF was originally computed from. This patcher is
 * opt-in since recalculating this data is unnecessary (and wasted time) for
 * meshes whose bounds are already correct.
 */
class PatcherMeshGlobalRecalculateBounds : public PatcherMeshGlobal {
public:
    /**
     * @brief Get the Factory object
     *
     * @return PatcherMeshGlobal::PatcherMeshGlobalFactory
     */
    static PatcherMeshGlobal::PatcherMeshGlobalFactory factory();

    /**
     * @brief Construct a new Patcher Mesh Global Recalculate Bounds patcher
     *
     * @param nifPath NIF path to be patched
     * @param nif NIF object to be patched
     */
    PatcherMeshGlobalRecalculateBounds(std::filesystem::path nifPath,
                                       nifly::NifFile* nif);

    /**
     * @brief Recompute the bounding sphere for every shape in the NIF
     *
     * @return true Bounds were recomputed for at least one shape
     * @return false NIF had no shapes to process
     */
    bool applyPatch() override;
};
