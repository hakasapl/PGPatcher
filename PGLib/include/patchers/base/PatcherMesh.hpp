#pragma once

#include "PGPlugin.hpp"
#include "Patcher.hpp"

#include "NifFile.hpp"

#include <filesystem>
#include <string>

/**
 * @class PatcherMesh
 * @brief Base class for all mesh patchers: holds the NIF being patched and the plugin use it is patched for
 *
 * Shapes of a NIF may share a shader, a BSShaderTextureSet or an NiAlphaProperty block. PGPatcher copies a shared
 * lighting shader for a shape before the shape is patched, and the setters of PGNIFUtil give a shape its own copy of a
 * shared texture set or alpha property before they change it, so a patch result does not reach the other shapes. A
 * block with an animation controller stays shared, and so does a change to the number of texture slots (see the note
 * on shared blocks in PGNIFUtil.hpp).
 */
class PatcherMesh : public Patcher {
private:
    // Instance vars.
    std::filesystem::path m_nifPath; /** Stores the path to the NIF file currently being patched */
    nifly::NifFile* m_nif; /** Stores the NIF object itself */
    PGPlugin::MeshUseAttributes m_meshUse; /** Stores the plugin use the NIF is currently being patched for */

protected:
    /**
     * @brief Get the plugin use the NIF is currently being patched for (used only within child patchers)
     *
     * A mesh without plugin uses, such as facegen or BodySlide ShapeData, is patched for a dummy use whose flags say
     * what kind of mesh it is.
     *
     * @return const PGPlugin::MeshUseAttributes& the mesh use
     */
    [[nodiscard]] const PGPlugin::MeshUseAttributes& meshUse() const;

public:
    /**
     * @brief Set the plugin use the NIF is being patched for
     *
     * @param meshUse the mesh use
     */
    void setMeshUse(const PGPlugin::MeshUseAttributes& meshUse);

protected:
    /**
     * @brief Get the NIF path for the current patcher (used only within child
     * patchers)
     *
     * @return std::filesystem::path Path to NIF
     */
    [[nodiscard]] std::filesystem::path nifPath() const;

    /**
     * @brief Get the NIF object for the current patcher (used only within child
     * patchers)
     *
     * @return nifly::NifFile* pointer to NIF object
     * @throws std::runtime_error if the patcher was created without a NIF
     */
    [[nodiscard]] nifly::NifFile* nif() const;

    /**
     * @brief Replace the NIF object the patcher works on
     */
    void setNIF(nifly::NifFile* nif);

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object, may be nullptr for patchers that only match texture sets
     * @param patcherName Name of patcher
     */
    PatcherMesh(std::filesystem::path nifPath,
                nifly::NifFile* nif,
                std::string patcherName);
};
