#pragma once

#include "PGPlugin.hpp"
#include "patchers/base/PatcherMesh.hpp"
#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

/**
 * @class PatcherMeshShader
 * @brief Base class for shader patchers, which find the textures of one shader type for a shape and apply them
 *
 * shouldApply() finds matches without a mesh, so it also serves the update cache; canApply() needs the mesh.
 */
class PatcherMeshShader : public PatcherMesh {
public:
    // Type definitions.
    using PatcherMeshShaderFactory
        = std::function<std::unique_ptr<PatcherMeshShader>(std::filesystem::path, nifly::NifFile*)>;
    using PatcherMeshShaderObject = std::unique_ptr<PatcherMeshShader>;

    /**
     * @struct PatcherMatch
     * @brief Structure to store the matched texture
     */
    struct PatcherMatch {
        std::wstring matchedPath; // The path of the matched file
        std::shared_ptr<void> extraData; // Any extra data the patcher might need intermally to do the patch
    };

    // Constructors.
    PatcherMeshShader(std::filesystem::path nifPath,
                      nifly::NifFile* nif,
                      std::string patcherName);
    virtual ~PatcherMeshShader() = default;
    PatcherMeshShader(const PatcherMeshShader& other) = default;
    PatcherMeshShader& operator=(const PatcherMeshShader& other) = default;
    PatcherMeshShader(PatcherMeshShader&& other) noexcept = default;
    PatcherMeshShader& operator=(PatcherMeshShader&& other) noexcept = default;

    /**
     * @brief Checks if a shape can take this patcher's shader (without looking at slots)
     *
     * @param nifShape Shape to check
     * @param isSinglepassMATO Whether the plugin use is a single pass material object
     * @param modelRecordType Plugin record type of the use
     * @return true Shape can be patched
     * @return false Shape cannot be patched
     */
    virtual bool canApply(nifly::NiShape& nifShape,
                          bool isSinglepassMATO,
                          const PGPlugin::ModelRecordType& modelRecordType) = 0;

    /// @brief Find the matches for a shape's texture set
    /// @param[in] nifShape shape to check
    /// @param[out] matches found matches
    /// @return if any match was found
    virtual bool shouldApply(nifly::NiShape& nifShape,
                             std::vector<PatcherMatch>& matches) = 0;

    /// @brief Find the matches for a texture set
    /// @param[in] oldSlots array of texture slot textures
    /// @param[out] matches vector of matches for the given textures
    /// @return if any match was found
    virtual bool shouldApply(const PGTypes::TextureSet& oldSlots,
                             std::vector<PatcherMatch>& matches) = 0;

    /// @brief Apply a match to a shape: its shader fields (applyShader()) and its texture slots (applyPatchSlots())
    /// @param[in,out] slots texture slots of the shape, updated with the match
    /// @param nifShape shape to patch
    /// @param[in] match match to apply
    virtual void applyPatch(PGTypes::TextureSet& slots,
                            nifly::NiShape& nifShape,
                            const PatcherMatch& match) = 0;

    /// @brief Apply a match to texture slots only
    /// @param[in,out] slots texture slots, updated with the match
    /// @param[in] match match to apply
    virtual void applyPatchSlots(PGTypes::TextureSet& slots,
                                 const PatcherMatch& match) = 0;

    /// @brief Set the shader type and flags of this patcher's shader on a shape
    /// @param nifShape shape to apply the shader to
    virtual void applyShader(nifly::NiShape& nifShape) = 0;

    /**
     * @brief Deterministic hash of everything in a match's extraData that influences the patch result.
     *
     * Incremental runs use this to detect that a shape's matches changed without re-reading the mesh, so any patcher
     * that stores data in PatcherMatch::extraData must override this and hash that data.
     *
     * @param match match whose extra data should be hashed
     * @return uint64_t hash (0 if the patcher does not use extra data)
     */
    [[nodiscard]] virtual uint64_t matchExtraDataHash([[maybe_unused]] const PatcherMatch& match) const { return 0; }
};
