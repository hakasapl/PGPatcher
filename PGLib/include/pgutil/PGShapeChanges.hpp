#pragma once

#include "pgutil/PGTypes.hpp"

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>

#include <optional>

/**
 * @brief What the patchers changed on a shape, for the exports that tell other tools how a mesh is patched (the
 * DynDOLOD file of PGLite).
 *
 * A State holds what of a shape the exports describe: its lighting shader and the textures it is drawn with. The rest
 * the patchers change (the alpha property, the vertex data, the extra data) is left out. changes() lists what differs
 * between the state of a shape before and after a patch, under the names nifly gives the blocks and their members,
 * with the raw values the mesh stores; a slot the patch emptied is listed as much as one it filled. Only what the mesh
 * holds differently after the patch is listed: a value a patcher writes over the same value is no change.
 */
namespace PGShapeChanges {

/// @brief What of a shape the exports describe, see PGShapeChanges.
struct State {
    /// @brief The lighting shader; nothing for a shape with another shader or none, which no patcher writes.
    std::optional<nifly::BSLightingShaderProperty> shader;
    /// @brief The textures the shape is drawn with: its texture set, or the alternate texture of a record.
    PGTypes::TextureSet slots;
};

/**
 * @brief The state of a shape
 *
 * @param nif NIF the shape belongs to
 * @param nifShape The shape
 * @param slots The textures the shape is drawn with, see State::slots
 * @return The state
 */
[[nodiscard]] State stateOf(const nifly::NifFile& nif,
                            nifly::NiShape& nifShape,
                            const PGTypes::TextureSet& slots);

/**
 * @brief What differs between two states of a shape, with the values of the second
 *
 * - "BSLightingShaderProperty": the shader fields that changed, see PGMaterialState::shaderChanges().
 * - "BSShaderTextureSet": the texture slots that changed, by their index in the texture set ("0" ... "8"), relative
 *   to the textures folder; an empty string for a slot that was emptied. A path with and without the textures folder
 *   is the same texture.
 *
 * @param base State before the patch
 * @param patched State after the patch
 * @return JSON object with the changes, empty when nothing changed
 */
[[nodiscard]] nlohmann::json changes(const State& base,
                                     const State& patched);

} // namespace PGShapeChanges
