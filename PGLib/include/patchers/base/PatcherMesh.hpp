#pragma once

#include "PGPlugin.hpp"
#include "Patcher.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <unordered_map>

/**
 * @class PatcherMesh
 * @brief Base class for all mesh patchers: holds the NIF being patched and the plugin use it is patched for
 *
 * Shapes of a NIF may share one BSShaderTextureSet block. The static texture set functions give every shape its own
 * patch result: a shared block is left with the first result and later shapes that need a different result get a copy.
 */
class PatcherMesh : public Patcher {
private:
    struct PatchedTextureSet {
        PGTypes::TextureSet original;
        std::unordered_map<uint32_t, PGTypes::TextureSet> patchResults;
    };

    static std::shared_mutex s_patchedTextureSetsMutex;
    static std::unordered_map<std::filesystem::path, std::unordered_map<uint32_t, PatchedTextureSet>>
        s_patchedTextureSets;

public:
    /**
     * @brief Get the texture set of a shape as it was before any shape sharing its block was patched
     *
     * @param nifPath Path of the NIF, key of the per NIF bookkeeping
     * @param nif NIF the shape belongs to
     * @param nifShape Shape with a shader that has a texture set
     * @return lower case texture paths by slot
     */
    static PGTypes::TextureSet textureSet(const std::filesystem::path& nifPath,
                                          nifly::NifFile& nif,
                                          nifly::NiShape& nifShape);

    /**
     * @brief Set the texture set of a shape, giving it its own BSShaderTextureSet block when the block it shares with
     * another shape was already patched to something else
     *
     * @param nifPath Path of the NIF, key of the per NIF bookkeeping
     * @param nif NIF the shape belongs to
     * @param nifShape Shape with a BSLightingShaderProperty that has a texture set
     * @param textures Textures to set
     * @return true if the shape's textures changed
     */
    static bool setTextureSet(const std::filesystem::path& nifPath,
                              nifly::NifFile& nif,
                              nifly::NiShape& nifShape,
                              const PGTypes::TextureSet& textures);

    /**
     * @brief Forgets the texture set bookkeeping of a NIF. Call once the NIF is done
     */
    static void clearTextureSets(const std::filesystem::path& nifPath);

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
