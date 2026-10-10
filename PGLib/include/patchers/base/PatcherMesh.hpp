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
 * @class Patcher
 * @brief Base class for all patchers
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
    static PGTypes::TextureSet textureSet(const std::filesystem::path& nifPath,
                                          nifly::NifFile& nif,
                                          nifly::NiShape& nifShape);
    static bool setTextureSet(const std::filesystem::path& nifPath,
                              nifly::NifFile& nif,
                              nifly::NiShape& nifShape,
                              const PGTypes::TextureSet& textures);
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
     */
    [[nodiscard]] nifly::NifFile* nif() const;

    void setNIF(nifly::NifFile* nif);

public:
    /**
     * @brief Construct a new Patcher object
     *
     * @param nifPath Path to NIF being patched
     * @param nif NIF object
     * @param patcherName Name of patcher
     */
    PatcherMesh(std::filesystem::path nifPath,
                nifly::NifFile* nif,
                std::string patcherName);
};
