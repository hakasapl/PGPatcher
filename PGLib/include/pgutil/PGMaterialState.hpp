#pragma once

#include "Geometry.hpp"
#include "NifFile.hpp"
#include "Shaders.hpp"
#include <nlohmann/json.hpp>

/**
 * @brief The material of a shape as the patchers see it: the fields of its BSLightingShaderProperty and its texture
 * set.
 *
 * One table lists every field the SSE mesh format stores for a BSLightingShaderProperty. The permutation tracker
 * compares materials with it to tell patched meshes apart, and the stock marker records and restores materials with
 * it, so a field a patcher writes cannot be covered by one and forgotten by the other.
 */
namespace PGMaterialState {

/**
 * @brief Serializes every shader field
 *
 * The keys follow the field names NifSkope shows for a BSLightingShaderProperty. Floats are rounded to nine
 * significant digits, which reads back as the same float.
 *
 * @param shader Shader to serialize
 * @return JSON object with one entry per field
 */
nlohmann::json shaderToJSON(const nifly::BSLightingShaderProperty& shader);

/**
 * @brief Applies every field present in the JSON to the shader
 *
 * A value that does not have the type of its field is ignored.
 *
 * @param shader Shader to change
 * @param fields JSON object as returned by shaderToJSON(), possibly with fields left out
 * @return true The shader changed
 * @return false Every field already had its value
 */
bool shaderFromJSON(nifly::BSLightingShaderProperty& shader,
                    const nlohmann::json& fields);

/**
 * @brief Compares every shader field exactly
 *
 * @param shaderA First shader
 * @param shaderB Second shader
 * @return true Every field is equal
 * @return false At least one field differs
 */
bool isShaderEqual(const nifly::BSLightingShaderProperty& shaderA,
                   const nifly::BSLightingShaderProperty& shaderB);

/**
 * @brief Serializes the texture slots of a shape
 *
 * @param nif NIF the shape belongs to
 * @param nifShape Shape to serialize
 * @return JSON object with the lower case path of every slot, keyed by slot index
 */
nlohmann::json textureSlotsToJSON(const nifly::NifFile& nif,
                                  nifly::NiShape& nifShape);

/**
 * @brief Compares two texture sets slot by slot without regard to case
 *
 * A slot one set has and the other does not counts as empty.
 *
 * @param texSetA First texture set
 * @param texSetB Second texture set
 * @return true Every slot is equal
 * @return false At least one slot differs
 */
bool isTextureSetEqual(const nifly::BSShaderTextureSet& texSetA,
                       const nifly::BSShaderTextureSet& texSetB);

} // namespace PGMaterialState
